#include <string.h>
#include <assert.h>
#include "util.h"
#include "request.h"

static void url_init(url_t *url)
{
    memset(url, 0, sizeof(*url));
}

void request_init(request_t *req)
{
    assert(req);

    req->method = GET;
    url_init(&req->url);
    req->http_version.major = req->http_version.minor = 0;

    memset(&req->header, 0, sizeof(req->header));
    str_init(&req->body);

    req->stage = PARSE_REQUEST_LINE;
    req->check_ch = req->recv_buf.begin;

    buffer_init(&req->recv_buf);
    buffer_init(&req->send_buf);
    req->resource_fd = -1;
    req->resource_size = 0;
}

/* request_line不能有\n
 * header 可以分为多行，每行开始至少有一个空格或\t
 */
static int parse_line(request_t *req)
{
    for (; req->check_ch < req->recv_buf.end; req->check_ch++) {

    }
}

static int parse_request_line(request_t *req)
{
}

static int parse_header(request_t *req)
{
}

static int parse_body(request_t *req)
{
}

int parse_request(request_t *req)
{
    assert(req);

    while (parse_line(req) == PARSE_OK) {
        switch (req->stage) {
        case PARSE_REQUEST_LINE:
            if (parse_request_line(req) != PARSE_OK)
                return PARSE_ERR;
            break;

        case PARSE_HEADER:
            if (parse_header(req) != PARSE_OK)
                return PARSE_ERR;
            break;

        case PARSE_BODY:
            if (parse_body(req) != PARSE_OK)
                return PARSE_ERR;
            break;

        default:
            MUSE_EXIT_ON(1, "unknown parse stage of request");
        }
    }
    return (req->stage == PARSE_DONE) ? PARSE_OK : PARSE_AGAIN;
}
