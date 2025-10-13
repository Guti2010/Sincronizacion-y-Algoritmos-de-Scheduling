#define _GNU_SOURCE
#include <pthread.h>
#include <stdatomic.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>
#include "ipc.h"
#include "queue.h"
#include "station.h"
#include "policy.h"

#define MAX_PRODS 4096
#define MAX_SLICES 20000

static inline double clip0(double x) { return x < 0.0 ? 0.0 : x; }
static inline double total_burst_s(const Product *p)
{
    return (p->svc_ms[0] + p->svc_ms[1] + p->svc_ms[2]) / 1000.0; // ms → s
}

/* =================== GENERADOR =================== */
/* Carga bursts por estación desde cfg y setea rem_ms = svc_ms (para RR) */
void generator_process(int out_fd, int count, const StationConfig svc_all[NSTAGES])
{
    LOG("generator", "inicio out=%d count=%d", out_fd, count);
    for (int i = 0; i < count; i++)
    {
        Product p = (Product){0};
        p.id = i + 1;
        p.arrival_s = (double)i; // 0,1,2,...
        for (int s = 0; s < NSTAGES; ++s)
        {
            p.svc_ms[s] = svc_all[s].work_ms; // burst por estación (común a todos)
            p.rem_ms[s] = svc_all[s].work_ms; // restante
        }
        write_full(out_fd, &p, sizeof(Product));
        LOG("generator", "enviado Product #%02d (arrival=%.0f)", p.id, p.arrival_s);
    }
    LOG("generator", "EOF out=%d", out_fd);
    close(out_fd);
    exit(0);
}

/* ====== Contexto de estación con cola ====== */
typedef struct
{
    int in_fd, out_fd;
    int idx;           // 0=E1, 1=E2, 2=E3
    StationConfig cfg; // {policy, work_ms, quantum_ms}
    ProductQueue *q;
    atomic_int *done; // lector terminó (EOF)
    // epoch global (solo lo fija E1 al primer ingreso)
    atomic_int *epoch_set; // 0->no fijado; 1->fijado
    double *epoch_value;   // valor del epoch (CLOCK_MONOTONIC)
} StationCtx;

/* Lector: consume del pipe y encola */
static void *th_reader(void *arg)
{
    StationCtx *cx = (StationCtx *)arg;
    Product p;
    while (read_full(cx->in_fd, &p, sizeof(Product)))
    {
        q_push(cx->q, &p);
    }
    atomic_store(cx->done, 1);
    sem_post(&cx->q->sem_items); // despertar worker si espera
    return NULL;
}

/* ----------------- Gantt por estación (en este proceso) ----------------- */
typedef struct
{
    int id;
    double t0, t1;
} Slice;
static Slice g_slices[MAX_SLICES];
static int g_nslices = 0;

static void gantt_add(int id, double t0, double t1)
{
    if (g_nslices < MAX_SLICES)
        g_slices[g_nslices++] = (Slice){id, t0, t1};
}
static int cmp_slice(const void *a, const void *b)
{
    double d = ((const Slice *)a)->t0 - ((const Slice *)b)->t0;
    return (d > 0) - (d < 0);
}
static void gantt_print(int station_idx)
{
    int n = g_nslices;
    printf("\n--- Gantt station%d ---\n", station_idx + 1);
    if (n == 0)
    {
        printf("(sin slices)\n");
        return;
    }
    qsort(g_slices, n, sizeof(Slice), cmp_slice);
    printf("Gantt: ");
    for (int i = 0; i < n; i++)
    {
        printf("%.3f–%.3f P%d%s",
               g_slices[i].t0, g_slices[i].t1, g_slices[i].id,
               (i + 1 < n) ? " | " : "\n");
    }
}

/* ----------------- Agregación global (solo en E3) ----------------- */
typedef struct
{
    int id;
    double arrival; // arrival_s
    double t_in[3], t_out[3];
    int svc_ms[3]; // bursts por estación
} Rec;

static Rec g_recs[MAX_PRODS];
static int g_rec_len = 0;

static int g_finish_order[MAX_PRODS];
static double g_finish_time[MAX_PRODS];
static int g_finish_len = 0;

static double g_sum_tat_total = 0.0;  // TAT total = t_out_E3 - arrival
static double g_sum_wait_total = 0.0; // WT total = TAT total - sum(bursts)
static int g_n_done_total = 0;

static void print_metrics(const Product *p)
{
    // Duración real por estación (para verificación)
    double e1 = p->t_out_s[0] - p->t_in_s[0];
    double e2 = p->t_out_s[1] - p->t_in_s[1];
    double e3 = p->t_out_s[2] - p->t_in_s[2];

    double tat_total = p->t_out_s[2] - p->arrival_s; // desde llegada hasta salida E3
    double wt_total = tat_total - total_burst_s(p);  // TAT - (sum bursts por estación)
    if (wt_total < 0)
        wt_total = 0.0;

    printf("    ↳ P#%02d | arrival=%.0f | "
           "E1[%.3f→%.3f](%.3fs)  E2[%.3f→%.3f](%.3fs)  E3[%.3f→%.3f](%.3fs)  "
           "| TAT=%.3fs  WT=%.3fs\n",
           p->id, p->arrival_s,
           p->t_in_s[0], p->t_out_s[0], e1,
           p->t_in_s[1], p->t_out_s[1], e2,
           p->t_in_s[2], p->t_out_s[2], e3,
           tat_total, wt_total);
}

/* ------------------ Persistencia/Resumen de IDs por estación ------------------ */

// Guarda SOLO IDs por slice (con repeticiones), ordenados por tiempo, en /tmp/assembly_stationX.ids
static void save_ids_sequence_to_tmp(int station_idx)
{
    int n = g_nslices;
    qsort(g_slices, n, sizeof(Slice), cmp_slice); // asegurar orden temporal

    char path[64];
    snprintf(path, sizeof(path), "/tmp/assembly_station%d.ids", station_idx + 1);

    FILE *f = fopen(path, "w");
    if (!f)
    {
        perror("fopen ids");
        return;
    }
    for (int i = 0; i < n; i++)
    {
        fprintf(f, "P%d%s", g_slices[i].id, (i + 1 < n) ? " -> " : "\n");
    }
    fclose(f);
}

// Extrae tokens "P<digits>" de una línea y los imprime concatenados con " -> ".
static void print_P_tokens_from_line(const char *line, int *printed_any)
{
    const char *p = line;
    while (*p)
    {
        if (*p == 'P')
        {
            const char *start = p;
            ++p;
            while (isdigit((unsigned char)*p))
                ++p; // avanzar dígitos del id
            if (*printed_any)
                printf(" -> ");
            fwrite(start, 1, (size_t)(p - start), stdout); // imprime "P123"
            *printed_any = 1;
        }
        else
        {
            ++p;
        }
    }
}

// Lee /tmp/assembly_station{1,2,3}.ids y los concatena en un solo orden:
// E1 (por slice, con repeticiones) seguido de E2 y luego E3.
static void print_all_stations_ids_together(void)
{
    const char *paths[3] = {
        "/tmp/assembly_station1.ids",
        "/tmp/assembly_station2.ids",
        "/tmp/assembly_station3.ids"};

    char line[65536];
    int printed_any = 0;

    printf("Orden de procesamiento (E1+E2+E3): ");

    for (int s = 0; s < 3; ++s)
    {
        FILE *f = fopen(paths[s], "r");
        if (!f)
            continue;
        if (fgets(line, sizeof(line), f))
        {
            // quitar salto de línea al final si lo hubiera
            size_t L = strlen(line);
            while (L > 0 && (line[L - 1] == '\n' || line[L - 1] == '\r'))
                line[--L] = '\0';
            print_P_tokens_from_line(line, &printed_any);
        }
        fclose(f);
    }

    if (!printed_any)
        printf("(sin datos)");
    printf("\n");
}

/* ------------------ Worker (FCFS / RR) ------------------ */
static void *th_worker(void *arg)
{
    StationCtx *cx = (StationCtx *)arg;

    for (;;)
    {
        if (atomic_load(cx->done) && cx->q->size == 0)
            break;

        Product p;
        q_pop(cx->q, &p); // FCFS a nivel de cola de llegada

        /* E1 fija el epoch y respeta arrival */
        if (cx->idx == 0)
        {
            if (!atomic_load(cx->epoch_set))
            {
                double epoch = now_s();
                *cx->epoch_value = epoch;
                atomic_store(cx->epoch_set, 1);
                LOG("station1", "epoch_s=%.6f fijado al entrar P#%02d", epoch, p.id);
            }
            if (p.epoch_s == 0.0)
                p.epoch_s = *cx->epoch_value;

            // gate de llegada: no entrar antes de arrival_s
            double elapsed = now_s() - p.epoch_s;
            if (elapsed < p.arrival_s)
            {
                int wait_ms = (int)((p.arrival_s - elapsed) * 1000.0 + 0.5);
                if (wait_ms > 0)
                    sleep_ms(wait_ms);
            }
        }
        else
        {
            if (p.epoch_s == 0.0)
                p.epoch_s = *cx->epoch_value;
        }

        // Marca de entrada solo la primera vez en esta estación
        if (p.t_in_s[cx->idx] <= 0.0)
            p.t_in_s[cx->idx] = now_s() - p.epoch_s;

        int *rem = &p.rem_ms[cx->idx];

        if (cx->cfg.policy == POL_FCFS)
        {
            // Un único slice con todo el servicio restante
            int work = *rem > 0 ? *rem : 0;
            if (work > 0)
            {
                double s0 = now_s() - p.epoch_s;
                sleep_ms(work);
                double s1 = now_s() - p.epoch_s;
                gantt_add(p.id, s0, s1);
                // marcar salida también
                p.t_out_s[cx->idx] = s1;
            }
            *rem = 0;
        }
        else
        { // ======= POL_RR =======
            // tamaño de quantum válido
            const int q = (cx->cfg.quantum_ms > 0) ? cx->cfg.quantum_ms : 1;

            // ejecutar SOLO un slice (preempción garantizada por diseño)
            const int slice = (*rem > q) ? q : *rem;
            if (slice > 0)
            {
                const double s0 = now_s() - p.epoch_s; // inicio del slice
                sleep_ms(slice);                       // simula ejecución por 'slice'
                const double s1 = now_s() - p.epoch_s; // fin del slice
                gantt_add(p.id, s0, s1);               // registrar slice en Gantt
                *rem -= slice;
            }
            // NO marcar salida aquí; lo haremos justo después si rem == 0
        }

        // Marcar salida (común a FCFS y RR) solo si ya no queda remanente
        if (*rem <= 0 && p.t_out_s[cx->idx] <= 0.0)
        {
            p.t_out_s[cx->idx] = now_s() - p.epoch_s;
        }

        if (cx->idx < 2)
        {
            // No es la última estación
            if (cx->cfg.policy == POL_RR && *rem > 0)
            {
                // RR con remanente: re-encolar en ESTA estación (preempción)
                q_push(cx->q, &p);
            }
            else
            {
                // Completó esta estación → pasa a la siguiente
                write_full(cx->out_fd, &p, sizeof(Product));
                LOG((cx->idx == 0) ? "station1" : "station2",
                    "P#%02d E%d done [%.3f→%.3f] → next",
                    p.id, cx->idx + 1, p.t_in_s[cx->idx], p.t_out_s[cx->idx]);
            }
        }
        else
        {
            // Estación 3
            if (cx->cfg.policy == POL_RR && *rem > 0)
            {
                // RR con remanente en E3: re-encolar aquí mismo
                q_push(cx->q, &p);
            }
            else
            {
                // Terminó E3 → guardar para resumen y mostrar métricas
                if (g_rec_len < MAX_PRODS)
                {
                    g_recs[g_rec_len].id = p.id;
                    g_recs[g_rec_len].arrival = p.arrival_s;
                    for (int s = 0; s < 3; ++s)
                    {
                        g_recs[g_rec_len].t_in[s] = p.t_in_s[s];
                        g_recs[g_rec_len].t_out[s] = p.t_out_s[s];
                        g_recs[g_rec_len].svc_ms[s] = p.svc_ms[s];
                    }
                    g_rec_len++;
                }

                double tat_total = p.t_out_s[2] - p.arrival_s;
                double wt_total = tat_total - total_burst_s(&p);
                if (wt_total < 0)
                    wt_total = 0.0;

                g_sum_tat_total += tat_total;
                g_sum_wait_total += wt_total;
                g_n_done_total += 1;

                if (g_finish_len < MAX_PRODS)
                {
                    g_finish_order[g_finish_len] = p.id;
                    g_finish_time[g_finish_len] = p.t_out_s[2];
                    g_finish_len++;
                }

                LOG("station3", "P#%02d E3 done [%.3f→%.3f] → fin",
                    p.id, p.t_in_s[2], p.t_out_s[2]);
                print_metrics(&p);
            }
        }
    }
    return NULL;
}

/* Arranque estándar de estación con cola (lector + worker) */
static void run_station_with_queue(int in_fd, int out_fd, int idx, StationConfig cfg,
                                   atomic_int *epoch_set, double *epoch_value)
{
    char role[16];
    snprintf(role, sizeof(role), "station%d", idx + 1);
    LOG(role, "inicio in=%d out=%d policy=%s work=%dms q=%d",
        in_fd, out_fd, (cfg.policy == POL_FCFS) ? "FCFS" : "RR", cfg.work_ms, cfg.quantum_ms);

    ProductQueue q;
    q_init(&q);
    atomic_int done = 0;

    StationCtx cx = {
        .in_fd = in_fd, .out_fd = out_fd, .idx = idx, .cfg = cfg, .q = &q, .done = &done, .epoch_set = epoch_set, .epoch_value = epoch_value};

    pthread_t tr, tw;
    pthread_create(&tr, NULL, th_reader, &cx);
    pthread_create(&tw, NULL, th_worker, &cx);

    pthread_join(tr, NULL);
    pthread_join(tw, NULL);

    // Gantt con tiempos (conservado)
    gantt_print(idx);

    // Guardar SOLO IDs por slice (con repeticiones) para esta estación
    save_ids_sequence_to_tmp(idx);

    // Resumen final (solo en E3)
    if (idx == 2)
    {
        printf("\n===== RESUMEN FINAL =====\n");

        // Línea única con el orden de procesamiento conjunto E1+E2+E3 (IDs por slice, con repeticiones)
        print_all_stations_ids_together();

        if (g_n_done_total > 0)
        {
            double avg_wtot = g_sum_wait_total / g_n_done_total;
            double avg_ttot = g_sum_tat_total / g_n_done_total;
            printf("Promedio de espera TOTAL (WT):      %.3fs\n", avg_wtot);
            printf("Promedio de turnaround TOTAL (TAT): %.3fs\n", avg_ttot);
        }
        else
        {
            printf("No se completaron productos en E3.\n");
        }
        printf("=========================\n");
    }

    q_destroy(&q);
    if (in_fd >= 0)
        close(in_fd);
    if (out_fd >= 0 && idx < 2)
        close(out_fd); // E3 no tiene siguiente
    LOG(role, "fin");
    exit(0);
}

/* Wrappers por estación */
void station1_with_queue(int in_fd, int out_fd, StationConfig cfg)
{
    static atomic_int epoch_set = 0;
    static double epoch_value = 0.0;
    run_station_with_queue(in_fd, out_fd, 0, cfg, &epoch_set, &epoch_value);
}
void station2_with_queue(int in_fd, int out_fd, StationConfig cfg)
{
    static atomic_int epoch_set = 1; // ya fijado por E1
    static double epoch_value = 0.0;
    run_station_with_queue(in_fd, out_fd, 1, cfg, &epoch_set, &epoch_value);
}
void station3_with_queue_and_metrics(int in_fd, StationConfig cfg)
{
    static atomic_int epoch_set = 1;
    static double epoch_value = 0.0;
    run_station_with_queue(in_fd, -1, 2, cfg, &epoch_set, &epoch_value);
}
