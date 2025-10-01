#include <unistd.h>
#include <sys/wait.h>
#include <stdlib.h>
#include "product.h"
#include "ipc.h"
#include "station.h"
#include "policy.h"

/*
 * Flujo con colas:
 *  GENERATOR --A--> E1(with_queue) --B--> E2(with_queue) --C--> E3(with_queue+metrics)
 *
 * Política por estación (elige aquí FCFS o RR y quantum):
 *  - En RR, el worker "rebana" y re-encola si hay remanente.
 *  - Siempre hay UN solo worker por estación => solo un producto en proceso.
 */
int main(void){
    setvbuf(stdout, NULL, _IONBF, 0);

    // Config de estaciones (E1 FCFS 400ms; E2 RR 600ms q=200; E3 RR 300ms q=200)
    StationConfig cfg[NSTAGES] = {
        { .policy = POL_RR, .work_ms = 400, .quantum_ms = 100   }, // E1
        { .policy = POL_RR,   .work_ms = 600, .quantum_ms = 200 }, // E2
        { .policy = POL_RR,   .work_ms = 300, .quantum_ms = 200 }  // E3
    };


    int A[2], B[2], C[2];
    if(pipe(A)<0 || pipe(B)<0 || pipe(C)<0){ perror("pipe"); return 1; }
    LOG("parent","pipes A(%d,%d) B(%d,%d) C(%d,%d)", A[0],A[1],B[0],B[1],C[0],C[1]);

    // --- generator: llena svc_ms/rem_ms según cfg[].work_ms ---
    pid_t g = fork();
    if(g<0){ perror("fork gen"); return 1; }
    if(g==0){
        close(A[0]); close(B[0]); close(B[1]); close(C[0]); close(C[1]);
        generator_process(A[1], /*count=*/10, cfg);
    }
    LOG("parent","generator pid=%d",(int)g);

    // --- E1 ---
    pid_t s1 = fork();
    if(s1<0){ perror("fork s1"); return 1; }
    if(s1==0){
        close(A[1]); close(B[0]); close(C[0]); close(C[1]);
        station1_with_queue(A[0], B[1], cfg[0]);
    }
    LOG("parent","station1 pid=%d",(int)s1);

    // --- E2 ---
    pid_t s2 = fork();
    if(s2<0){ perror("fork s2"); return 1; }
    if(s2==0){
        close(A[0]); close(A[1]); close(B[1]); close(C[0]);
        station2_with_queue(B[0], C[1], cfg[1]);
    }
    LOG("parent","station2 pid=%d",(int)s2);

    // --- E3 ---
    pid_t s3 = fork();
    if(s3<0){ perror("fork s3"); return 1; }
    if(s3==0){
        close(A[0]); close(A[1]); close(B[0]); close(B[1]); close(C[1]);
        station3_with_queue_and_metrics(C[0], cfg[2]);
    }
    LOG("parent","station3 pid=%d",(int)s3);

    // --- cerrar y esperar ---
    close(A[0]); close(A[1]); close(B[0]); close(B[1]); close(C[0]); close(C[1]);
    LOG("parent","cierro FDs; esperando hijos...");
    int st;
    while (wait(&st) > 0) {}
    LOG("parent","todos terminaron");
    return 0;
}
