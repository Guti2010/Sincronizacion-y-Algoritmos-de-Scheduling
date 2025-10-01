#ifndef POLICY_H
#define POLICY_H

typedef enum {
    POL_FCFS = 0,   // atiende un producto hasta terminar su servicio
    POL_RR   = 1    // atiende por quantum y re-encola si resta servicio
} SchedPolicy;

typedef struct {
    SchedPolicy policy;   // FCFS o RR
    int work_ms;          // tiempo de servicio de esa estación (ms)
    int quantum_ms;       // quantum para RR (ms); ignorado en FCFS
} StationConfig;

#endif /* POLICY_H */
