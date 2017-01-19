#ifndef MUSE_REQUEST_H__
#define MUSE_REQUEST_H__

typedef struct {

} request_t;

void request_init(request_t *req);

int handle_request(void *ptr);

#endif
