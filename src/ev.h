#ifndef MUSE_EV_H__
#define MUSE_EV_H__

typedef struct {
    void *ptr;
    int (*in_handler)(void *ptr);
    int (*out_handler)(void *ptr);
    void (*ok_handler)(void *ptr);
    void (*err_handler)(void *ptr);
} ev_t;

#endif
