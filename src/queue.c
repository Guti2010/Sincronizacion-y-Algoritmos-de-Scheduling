#include "queue.h"

int q_init(ProductQueue* q){
    q->head=q->tail=q->size=0;
    pthread_mutex_init(&q->mtx, NULL);
    sem_init(&q->sem_items, 0, 0);
    sem_init(&q->sem_space, 0, QCAP);
    return 0;
}
void q_destroy(ProductQueue* q){
    pthread_mutex_destroy(&q->mtx);
    sem_destroy(&q->sem_items);
    sem_destroy(&q->sem_space);
}
void q_push(ProductQueue* q, const Product* p){
    sem_wait(&q->sem_space);
    pthread_mutex_lock(&q->mtx);
    q->buf[q->tail] = *p;
    q->tail = (q->tail + 1) % QCAP;
    q->size++;
    pthread_mutex_unlock(&q->mtx);
    sem_post(&q->sem_items);
}
void q_pop(ProductQueue* q, Product* out){
    sem_wait(&q->sem_items);
    pthread_mutex_lock(&q->mtx);
    *out = q->buf[q->head];
    q->head = (q->head + 1) % QCAP;
    q->size--;
    pthread_mutex_unlock(&q->mtx);
    sem_post(&q->sem_space);
}
