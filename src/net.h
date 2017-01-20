#ifndef MUSE_NET_H__
#define MUSE_NET_H__

int set_nonblocking(int fd);
int tcp_listen_fd(const char *addr, int port, int backlog);
int set_tcp_cork(int fd);
int reset_tcp_cork(int fd);

#endif
