#ifndef PRODUCT_H
#define PRODUCT_H
#include <stdint.h>

/*
 * Un "job" es una unidad de trabajo que pasa por etapas; aquí es un producto.
 * Regla de tiempo: epoch_s es el "cero global", fijado cuando el Producto 1
 * (arrival=0) ENTRA en la Estación 1. Todos los t_in/t_out van relativos a eso.
 *
 * Para soportar FCFS y RR, cada producto lleva:
 *  - svc_ms[i]: tiempo de servicio requerido en la estación i (ms).
 *  - rem_ms[i]: tiempo restante por estación i (ms) — se usa en RR.
 */

#define NSTAGES 3  // 0=E1, 1=E2, 2=E3

typedef struct {
    int32_t id;                 // 1..N
    double  arrival_s;          // llegada declarada al generar (0..N-1)
    double  epoch_s;            // cero global (se fija en E1 con el primer ingreso)

    // métricas relativas a epoch_s
    double  t_in_s[NSTAGES];    // entrada a estación i
    double  t_out_s[NSTAGES];   // salida de estación i

    // servicio/reste (ms) por estación
    int32_t svc_ms[NSTAGES];    // tiempo de servicio requerido
    int32_t rem_ms[NSTAGES];    // tiempo restante (RR lo va disminuyendo)
} Product;

#endif /* PRODUCT_H */
