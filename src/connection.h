#ifndef MUSE_CONNECTION_H__
#define MUSE_CONNECTION_H__

#include <sys/epoll.h>
#include <sys/types.h>
#include "request.h"

typedef struct {
    int fd;
    struct epoll_event event;
    request_t req;
    time_t active_time;
    int heap_idx;
} connection_t;

int accept_connection(void *listen_fd);
void close_connection(void *c);
void sweep_connection(void);

#endif
