#ifndef STATION_H
#define STATION_H
#include "product.h"
#include "policy.h"

/* Generador: crea N productos (arrival 0..N-1) con svc_ms predefinidos. */
void generator_process(int out_fd, int count,
                       const StationConfig svc_all[NSTAGES]);

/* Estaciones con cola interna (lector + worker único):
   - station1 fija epoch_s al primer ingreso de producto.
   - station2 y station3 usan el epoch ya fijado.
   - La política se elige por StationConfig (FCFS o RR). */
void station1_with_queue(int in_fd, int out_fd, StationConfig cfg);
void station2_with_queue(int in_fd, int out_fd, StationConfig cfg);
void station3_with_queue_and_metrics(int in_fd, StationConfig cfg);

#endif /* STATION_H */
