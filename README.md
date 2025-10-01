Línea de Ensamblaje con C, POSIX y Docker

Simulación de una línea de ensamblaje con tres estaciones (E1, E2, E3) usando C, pipes, colas (mutex + semáforos POSIX), fork() + pthread, y políticas FCFS y Round Robin (RR con quantum configurable).
Se ejecuta con Docker y Docker Compose (sin Makefile).

Características

3 estaciones en procesos separados (cada una con 1 hilo lector + 1 hilo worker).

Comunicación entre estaciones vía pipes; dentro de cada estación hay una cola thread-safe (productor/consumidor).

FCFS (E1) y Round Robin (E2/E3) con quantum configurable (p. ej. 200 ms).

Bursts por estación (fijos y comunes a todos los productos): p. ej. E1=400 ms, E2=600 ms, E3=300 ms → burst total por producto = 1.300 s.

Tiempos de llegada simulados: 0, 1, 2, … s.

Registro de slices para imprimir un Gantt por estación (inicio–fin por producto).

Métricas por producto:

arrival_s

t_in_s[i] / t_out_s[i] (i=0..2)

TAT total = t_out_E3 − arrival_s

WT total = TAT total − (burst_E1 + burst_E2 + burst_E3)

Resumen final: promedio WT, promedio TAT, y orden final (IDs en el orden en que salen de E3).

Requisitos

Docker ≥ 20.x

Docker Compose V2

Cómo ejecutar
docker compose up --build


La primera vez compila el binario y arranca el contenedor. Las siguientes:

docker compose up


Para limpiar:

docker compose down --volumes

Estructura del repositorio
.
├─ docker-compose.yml
├─ Dockerfile
├─ src/
│  ├─ main.c
│  ├─ station.c
│  ├─ queue.c
│  └─ ipc.c
├─ include/
│  ├─ product.h
│  ├─ queue.h
│  ├─ ipc.h
│  ├─ station.h
│  └─ policy.h
└─ README.md

Archivos clave

include/product.h — struct Product
Campos esperados:

int id;

double arrival_s; (0..N−1)

double epoch_s; (fijado por E1 al primer ingreso)

double t_in_s[3], t_out_s[3]; (tiempos relativos a epoch_s)

int svc_ms[3], rem_ms[3]; (burst por estación y restante para RR)

include/policy.h — Políticas de scheduling

typedef enum { POL_FCFS=0, POL_RR=1 } SchedPolicy;
typedef struct { SchedPolicy policy; int work_ms; int quantum_ms; } StationConfig;


src/station.c

generator_process(...): crea N productos, setea arrival_s = 0..N-1 y carga svc_ms/rem_ms desde StationConfig.

Estaciones station{1,2,3}_with_queue...(...): proceso por estación con hilo lector (pipe→cola) y hilo worker (cola→CPU).

RR ejecuta slices de tamaño min(rem, quantum) y re-encola si queda remanente (preempción).

E1 fija epoch_s y respeta arrival_s (no procesa antes de llegar).

Imprime Gantt por estación y, en E3, resumen con promedios y orden final.

src/queue.c / include/queue.h — Cola con pthread_mutex_t y sem_t (productor/consumidor).

src/ipc.c / include/ipc.h — Utilidades: now_s(), sleep_ms(int), read_full, write_full, LOG(...).

src/main.c — Crea pipes y fork() por proceso, configura StationConfig, lanza generador y estaciones, y espera su finalización.

Configuración de políticas (ejemplo recomendado)
// src/main.c
StationConfig cfg[NSTAGES] = {
  { .policy = POL_FCFS, .work_ms = 400, .quantum_ms = 0   }, // E1 (FCFS)
  { .policy = POL_RR,   .work_ms = 600, .quantum_ms = 200 }, // E2 (RR, q=200)
  { .policy = POL_RR,   .work_ms = 300, .quantum_ms = 200 }  // E3 (RR, q=200)
};


Con esta configuración, el burst total por producto es 1.300 ms (1.3 s).

Protocolo de operación (pCOL)

Generación y llegada

El generador crea N productos (id=1..N) con arrival_s = 0,1,2,….

Carga en cada Product los bursts por estación: svc_ms[i] = work_ms y rem_ms[i] = work_ms.

Envía por pipe hacia E1.

Epoch y tiempos relativos

E1 fija epoch_s cuando entra el primer producto.

Todos los t_in_s/t_out_s se miden relativos a epoch_s.

Gate de llegada

En E1, antes de ejecutar, si (now − epoch_s) < arrival_s se espera (no se procesa antes de la llegada simulada).

Cola por estación

Hilo lector mete productos del pipe a la cola.

Hilo worker toma de la cola (uno a la vez) y simula el servicio.

Servicio por política

FCFS: un solo slice de duración rem_ms (se agota el burst).

RR: un slice por quantum min(rem_ms, quantum_ms); si queda rem_ms>0, se re-encola (preempción).

Flujo entre estaciones

Al terminar E1 → pipe a E2; E2 → E3.

E3 es el punto final (imprime métricas y acumula resumen).

Métricas

Por producto:

arrival_s, t_in_s[i], t_out_s[i]

TAT_total = t_out_s[2] − arrival_s

WT_total = TAT_total − (svc_ms[0]+svc_ms[1]+svc_ms[2])/1000.0

Resumen (en E3):

avg(WT_total), avg(TAT_total)

Orden final: IDs en el orden de salida de E3.

Gantt por estación

Cada estación registra slices (t0–t1) por producto.

Al terminar, se imprime el Gantt local:

--- Gantt station2 ---
Gantt: 0.800–1.000 P1 | 1.000–1.200 P2 | 1.200–1.400 P1 | ...

Ejemplo de salida (fragmento)
station1: epoch_s=12345.678901 fijado al entrar P#01
station1: P#01 E1 done [0.000→0.400] → next
...
station3: P#01 E3 done [1.800→2.100] → fin
    ↳ P#01 | arrival=0 | E1[0.000→0.400](0.400s)  E2[0.800→1.400](0.600s)  E3[1.800→2.100](0.300s)  | TAT=2.100s  WT=0.800s
...

--- Gantt station1 ---
Gantt: 0.000–0.400 P1 | 1.000–1.400 P2 | 2.000–2.400 P3 | ...

--- Gantt station2 ---
Gantt: 0.800–1.000 P1 | 1.000–1.200 P2 | 1.200–1.400 P1 | ...

--- Gantt station3 ---
Gantt: 1.800–2.000 P1 | 2.000–2.100 P1 | 2.100–2.300 P2 | ...

===== RESUMEN FINAL =====
Promedio de espera TOTAL (WT):     0.812s
Promedio de turnaround TOTAL (TAT): 2.145s
Orden final de procesamiento: P1 -> P2 -> P3 -> P4 -> P5 -> P6 -> P7 -> P8 -> P9 -> P10
=========================


Los números exactos varían según quantum y tiempos.

Troubleshooting

TAT/WT negativos
Activa el gate de llegada en E1 (no ejecutar antes de arrival_s).
Recuerda que todos los tiempos se miden relativos a epoch_s.

RR “se ve” como FCFS
Si no hay otros listos en cola, tras re-encolar vuelve a salir el mismo producto; es normal.
Con más productos y llegadas cercanas verás la intercalación.

Conflicto con <sched.h>
El header de políticas se llama policy.h; evita crear include/sched.h que colisione con glibc.

Extensiones sugeridas

Flags CLI: cambiar política/quantum/bursts sin recompilar (p. ej. --e2=rr,150).

Exportar métricas a CSV.

Métricas por estación al estilo Cap. 5 (WT_i = TAT_i − burst_i).

Bursts variables por producto (no solo por estación).
