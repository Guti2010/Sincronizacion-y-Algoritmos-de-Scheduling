#ifndef IPC_H
#define IPC_H

#include <stddef.h>
#include <stdio.h>
#include <time.h>
#include <unistd.h>
#include <stdlib.h>

static inline double now_s(void){
    struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec/1e9;
}
static inline void sleep_ms(int ms){
    struct timespec ts = { ms/1000, (ms%1000)*1000000L };
    nanosleep(&ts, NULL);
}
static inline void write_full(int fd, const void* buf, size_t n){
    const char* p=(const char*)buf; size_t off=0;
    while(off<n){
        ssize_t k=write(fd,p+off,n-off);
        if(k<0){ perror("write"); exit(1); }
        off+=(size_t)k;
    }
}
static inline int read_full(int fd, void* buf, size_t n){
    char* p=(char*)buf; size_t off=0;
    while(off<n){
        ssize_t k=read(fd,p+off,n-off);
        if(k<0){ perror("read"); exit(1); }
        if(k==0) return 0; // EOF
        off+=(size_t)k;
    }
    return 1;
}

#define LOG(role, fmt, ...) \
    do { \
        printf("[%.3f][pid=%d][%s] " fmt "\n", now_s(), (int)getpid(), role, ##__VA_ARGS__); \
    } while(0)

#endif /* IPC_H */
