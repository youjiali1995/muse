#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include <assert.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include "buffer.h"

void buffer_init(buffer_t *buffer)
{
    assert(buffer);
    buffer->begin = buffer->end = buffer->buf;
}

int buffer_recv(buffer_t *buffer, int fd)
{
    assert(buffer);
    while (!buffer_full(buffer)) {
        ssize_t len = recv(fd, buffer->end, buffer_space(buffer), 0);
        if (len == 0)
            return BUF_CLOSE;
        if (len == -1)
            return (errno == EAGAIN || errno == EWOULDBLOCK) ? BUF_AGAGIN : BUF_ERR;
        buffer->end += len;
    }
    return BUF_AGAGIN;
}

int buffer_send(buffer_t *buffer, int fd)
{
    assert(buffer);
    while (buffer_size(buffer)) {
        ssize_t len = send(fd, buffer->begin, buffer_size(buffer), 0);
        if (len == -1)
            return (errno == EAGAIN || errno == EWOULDBLOCK) ? BUF_AGAGIN : BUF_ERR;
        buffer->begin += len;
    }
    buffer_clear(buffer);
    return BUF_OK;
}

int buffer_printf(buffer_t *buffer, const char *fmt, ...)
{
    assert(buffer && fmt);

    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buffer->end, buffer_space(buffer), fmt, ap);
    buffer->end += n;
    va_end(ap);
    return n;
}

#define MIN(a, b) ((a) < (b) ? (a) : (b))

int buffer_append_str(buffer_t *buffer, const str_t *str)
{
    assert(buffer && str);

    int len = MIN(buffer_space(buffer), str->len);
    memcpy(buffer->end, str->str, len);
    buffer->end += len;
    return len;
}

