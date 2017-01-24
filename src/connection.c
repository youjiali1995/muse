#include <unistd.h>
#include <time.h>
#include <sys/socket.h>
#include <sys/sendfile.h>
#include <stdlib.h>
#include <errno.h>
#include "server.h"
#include "connection.h"
#include "net.h"
#include "util.h"
#include "ev.h"

#define PARENT(i) ((i - 1) / 2)
#define LEFT(i) ((i) * 2 + 1)
#define RIGHT(i) ((i) * 2 + 2)

static int heap_size = 0;
static connection_t *connections[MAX_CONNECTIONS] = {NULL};

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
    if (epoll_ctl(epfd, EPOLL_CTL_ADD, c->fd, &c->event) == MUSE_ERR)
        close_connection(c);
    if (set_nonblocking(connfd) == MUSE_ERR)
        close_connection(c);
}

int accept_connection(void *listen_fd)
{
    int fd = *((int *) listen_fd);
    int connfd;

    while ((connfd = accept(fd, NULL, NULL)) != MUSE_ERR) {
        if (heap_size < MAX_CONNECTIONS)
            open_connection(connfd);
        else
            close(connfd);
    }
    MUSE_ERR_ON(errno != EWOULDBLOCK, strerror(errno), MUSE_ERR);
    return MUSE_OK;
}

void close_connection(void *ptr)
{
    connection_t *c = ptr;
    connections[c->heap_idx] = connections[--heap_size];
    connections[c->heap_idx]->heap_idx = c->heap_idx;
    heap_shift_down(c->heap_idx);

    close(c->fd);
    if (c->req.resource_fd != -1)
        close(c->req.resource_fd);
    free(c->event.data.ptr); /* ev_t */
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

static int connection_enable_in(connection_t *c)
{
    if (!(c->event.events & EPOLLIN)) {
        c->event.events |= EPOLLIN;
        MUSE_ERR_ON(epoll_ctl(epfd, EPOLL_CTL_MOD, c->fd, &c->event) == MUSE_ERR,
                strerror(errno), MUSE_ERR);
    }
    return MUSE_OK;
}

static int connection_disable_in(connection_t *c)
{
    if (c->event.events & EPOLLIN) {
        /* c->event.events &= ~EPOLLIN; */
        c->event.events ^= EPOLLIN;
        MUSE_ERR_ON(epoll_ctl(epfd, EPOLL_CTL_MOD, c->fd, &c->event) == MUSE_ERR,
                strerror(errno), MUSE_ERR);
    }
    return MUSE_OK;
}

static int connection_enable_out(connection_t *c)
{
    if (!(c->event.events & EPOLLOUT)) {
        c->event.events |= EPOLLOUT;
        MUSE_ERR_ON(epoll_ctl(epfd, EPOLL_CTL_MOD, c->fd, &c->event) == MUSE_ERR,
                strerror(errno), MUSE_ERR);
    }
    return MUSE_OK;
}

static int connection_disable_out(connection_t *c)
{
    if (c->event.events & EPOLLOUT) {
        c->event.events ^= EPOLLOUT;
        MUSE_ERR_ON(epoll_ctl(epfd, EPOLL_CTL_MOD, c->fd, &c->event) == MUSE_ERR,
                strerror(errno), MUSE_ERR);
    }
    return MUSE_OK;
}

int handle_request(void *ptr)
{
    connection_t *c = ptr;
    if (buffer_recv(&c->req.recv_buf, c->fd) != BUF_AGAGIN)
        return MUSE_ERR;

    switch (parse_request(&c->req)) {
    case PARSE_AGAIN:
        break;

    case PARSE_ERR:
    case PARSE_OK:
        connection_disable_in(c);
        connection_enable_out(c);
        break;

    default:
        MUSE_EXIT_ON(1, "unknown value returned by parse_request");
    }
    return MUSE_OK;
}

int handle_response(void *ptr)
{
    connection_t *c = ptr;
    /* sendfile 常与TCP_CORK一起使用，但没有body时，TCP_CORK可能会延迟,
     * 所以只在有文件时设置，重复设置也没问题。
     */
    if (c->req.resource_fd != -1)
        set_tcp_cork(c->fd);

    /* buffer为空会立即返回BUF_OK */
    int ret = buffer_send(&c->req.send_buf, c->fd);
    if (ret == BUF_ERR)
        return MUSE_ERR;
    else if (ret == BUF_AGAGIN)
        return MUSE_OK;

    /* 发送完首部 */
    if (c->req.resource_fd != -1) {
        for (;;) {
            /* sendfile 会改变文件偏移 */
            ssize_t len = sendfile(c->fd, c->req.resource_fd, NULL, c->req.resource_size);
            if (len == -1)
                return (errno == EAGAIN || errno == EWOULDBLOCK) ? MUSE_OK : MUSE_ERR;
            if (len == 0) {
                close(c->req.resource_fd);
                reset_tcp_cork(c->fd);
                break;
            }
        }
    }

    /* TODO: parse 出错怎么处理？直接关闭连接？ */
    /* response发送完毕 */
    connection_disable_out(c);
    connection_enable_in(c);
    request_clear(&c->req);

    return MUSE_OK;
}
