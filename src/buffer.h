#ifndef MUSE_BUFFER_H__
#define MUSE_BUFFER_H__

#define BUF_ERR (-1)
#define BUF_OK (0)
#define BUF_AGAGIN (1)
#define BUF_CLOSE (2)

#define BUF_SIZE (4096)

typedef struct {
    char *begin;
    char *end;
    char buf[BUF_SIZE];
} buffer_t;


#define buffer_clear(buffer) buffer_init(buffer)
#define buffer_size(buffer) ((buffer)->end - (buffer)->begin)
#define buffer_space(buffer) ((buffer)->buf + BUF_SIZE - (buffer)->end)
#define buffer_full(buffer) ((buffer)->end >= (buffer)->buf + BUF_SIZE)

void buffer_init(buffer_t *buffer);
int buffer_recv(buffer_t *buffer, int fd);
int buffer_send(buffer_t *buffer, int fd);

#endif
