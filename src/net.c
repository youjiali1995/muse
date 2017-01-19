#define _POSIX_SOURCE /* for c99 */
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include "net.h"
#include "util.h"

int set_nonblocking(int fd)
{
    int flag = fcntl(fd, F_GETFL);
    MUSE_ERR_ON(flag == -1, strerror(errno), MUSE_ERROR);
    flag |= O_NONBLOCK;
    MUSE_ERR_ON(fcntl(fd, F_SETFL, flag) == -1, strerror(errno), MUSE_ERROR);
    return MUSE_OK;
}

static int set_reuseport(int fd)
{
    int on = 1;
    MUSE_ERR_ON(setsockopt(fd, SOL_SOCKET, SO_REUSEPORT, &on, sizeof(on)) == -1,
            strerror(errno), MUSE_ERROR);
    return MUSE_OK;
}

int tcp_listen_fd(const char *addr, int port, int backlog)
{
    int ret, listen_fd;
    char _port[6];
    struct addrinfo hints, *res, *res_save;

    snprintf(_port, 6, "%d", port);
    memset(&hints, 0, sizeof(hints));
    hints.ai_flags = AI_PASSIVE;
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    ret = getaddrinfo(addr, _port, &hints, &res);
    MUSE_ERR_ON(ret != 0, gai_strerror(ret), MUSE_ERROR);

    for (res_save = res; res; res = res->ai_next) {
        listen_fd = socket(res->ai_family, res->ai_socktype | SOCK_NONBLOCK, res->ai_protocol);
        if (listen_fd == -1)
            continue;
        /* 内核帮助实现简单的负载均衡 */
        if (set_reuseport(listen_fd) == MUSE_ERROR) {
            close(listen_fd);
            continue;
        }
        if (bind(listen_fd, res->ai_addr, res->ai_addrlen) == -1) {
            close(listen_fd);
            continue;
        }
        if (listen(listen_fd, backlog) == -1) {
            close(listen_fd);
            continue;
        }
        break;
    }

    freeaddrinfo(res_save);
    return res ? listen_fd : MUSE_ERROR;
}
