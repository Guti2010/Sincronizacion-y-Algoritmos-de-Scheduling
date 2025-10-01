#ifndef QUEUE_H
#define QUEUE_H
#include <pthread.h>
#include <semaphore.h>
#include "product.h"

#define QCAP 128  // capacidad fija

typedef struct {
    Product buf[QCAP];
    int head, tail, size;
    pthread_mutex_t mtx;
    sem_t sem_items; // cuántos elementos hay
    sem_t sem_space; // cuántos espacios libres
} ProductQueue;

int  q_init(ProductQueue* q);
void q_destroy(ProductQueue* q);
void q_push(ProductQueue* q, const Product* p); // bloquea si llena
void q_pop(ProductQueue* q, Product* out);      // bloquea si vacía

#endif /* QUEUE_H */
