#include <unistd.h>
#include <time.h>
#include <sys/socket.h>
#include <stdlib.h>
#include <errno.h>
#include "server.h"
#include "response.h"
#include "connection.h"
#include "net.h"
#include "util.h"
#include "ev.h"

static int heap_size = 0;
static connection_t *connections[MAX_CONNECTIONS] = {NULL};

static void register_connection(connection_t *c);
static void active_connection(void *ptr);

static void open_connection(int connfd)
{
    connection_t *c = malloc(sizeof(*c));
    if (!c)
        close(connfd);
    register_connection(c);

    c->fd = connfd;
    request_init(&c->req);
    c->active_time = time(NULL);

    ev_t *ev = malloc(sizeof(*ev));
    if (!ev)
        close_connection(c);
    ev->ptr = c;
    ev->in_handler = handle_request;
    ev->out_handler = handle_response;
    ev->ok_handler = active_connection;
    ev->err_handler = close_connection;
    c->event.events = EPOLLIN;
    c->event.data.ptr = ev;
    if (epoll_ctl(epfd, EPOLL_CTL_ADD, c->fd, &c->event) == MUSE_ERROR)
        close_connection(c);
    if (set_nonblocking(connfd) == MUSE_ERROR)
        close_connection(c);
}

#define PARENT(i) ((i - 1) / 2)
#define LEFT(i) ((i) * 2 + 1)
#define RIGHT(i) ((i) * 2 + 2)

static void heap_shift_up(int idx)
{
    connection_t *c = connections[idx];
    while (idx > 0 && connections[PARENT(idx)]->active_time > c->active_time) {
        connections[idx] = connections[PARENT(idx)];
        connections[idx]->heap_idx = idx;
        idx = PARENT(idx);
    }
    connections[idx] = c;
    connections[idx]->heap_idx = idx;
}

static void heap_shift_down(int idx)
{
    connection_t *c = connections[idx];

    for (;;) {
        int smallest;
        if (LEFT(idx) < heap_size && connections[LEFT(idx)]->active_time < c->active_time)
            smallest = LEFT(idx);
        else
            smallest = idx;
        if (RIGHT(idx) < heap_size
                && connections[RIGHT(idx)]->active_time < connections[smallest]->active_time)
            smallest = RIGHT(idx);
        if (smallest == idx)
            break;
        connections[idx] = connections[smallest];
        connections[idx]->heap_idx = idx;
        idx = smallest;
    }
}

/* 时间堆：最小堆 */
static void register_connection(connection_t *c)
{
    connections[heap_size] = c;
    heap_shift_up(heap_size++);
}

static void active_connection(void *ptr)
{
    connection_t *c = ptr;
    c->active_time = time(NULL);
    heap_shift_down(c->heap_idx);
}

int accept_connection(void *listen_fd)
{
    int fd = *((int *) listen_fd);
    int connfd;

    while ((connfd = accept(fd, NULL, NULL)) != MUSE_ERROR) {
        if (heap_size < MAX_CONNECTIONS)
            open_connection(connfd);
        else
            close(connfd);
    }
    MUSE_ERR_ON(errno != EWOULDBLOCK, strerror(errno), MUSE_ERROR);
    return MUSE_OK;
}

void close_connection(void *ptr)
{
    connection_t *c = ptr;
    connections[c->heap_idx] = connections[--heap_size];
    connections[c->heap_idx]->heap_idx = c->heap_idx;
    heap_shift_down(c->heap_idx);

    close(c->fd);
    free(c->event.data.ptr);
    free(c);
}

void sweep_connection(void)
{
    while (heap_size > 0) {
        connection_t *c = connections[0];
        if (time(NULL) < c->active_time + server_cfg.timeout)
            break;
        close_connection(c);
    }
}
