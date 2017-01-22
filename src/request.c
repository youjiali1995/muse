#include <string.h>
#include <strings.h>
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

static int parse_line(request_t *req)
{
    for (; req->check_ch < req->recv_buf.end; req->check_ch++) {
        char ch = *req->check_ch;
        switch (ch) {
        case '\r':
            if (req->check_ch + 1 < req->recv_buf.end) {
                if (*(req->check_ch + 1) != '\n')
                    return PARSE_ERR;
                else {
                    req->check_ch += 2;
                    return PARSE_OK;
                }
            }
            return PARSE_AGAIN;

        case '\n':
            /* request line 不能有\n */
            if (req->stage == PARSE_REQUEST_LINE)
                return PARSE_ERR;
            /* header 可以有\n，不过在新行开始需要有空格或\t */
            if (req->stage == PARSE_HEADER) {
                if (req->check_ch + 1 < req->recv_buf.end) {
                    if (*(req->check_ch + 1) != ' ' && *(req->check_ch + 1) != '\t')
                        return PARSE_ERR;
                    else {
                        req->check_ch += 2;
                        return PARSE_OK;
                    }
                }
                return PARSE_AGAIN;
            }
            break;

        default:
            break;
        }
    }
    return PARSE_AGAIN;
}

static char *parse_whitespace(request_t *req, char *p)
{
    for (p; p < req->check_ch && *p == ' '; p++)
        ;
    req->recv_buf.begin = p;
    return p;
}

static int parse_method(request_t *req, char *end)
{
    assert(req->stage == PARSE_METHOD);
    char *begin = req->recv_buf.begin;
    int len = end - begin;

    /* method 大小写敏感, 其余大小写不敏感 */
    switch (len) {
    case 3:
        if (!strncmp(begin, "GET",  3)) {
            req->method = GET;
            break;
        } else if (!strncmp(begin, "PUT", 3)) {
            req->method = PUT;
            break;
        }
        return PARSE_ERR;

    case 4:
        if (!strncmp(begin, "HEAD", 4)) {
            req->method = HEAD;
            break;
        } else if (!strncmp(begin, "POST", 4)) {
            req->method = POST;
            break;
        }
        return PARSE_ERR;

    case 5:
        if (!strncmp(begin, "TRACE", 5)) {
            req->method = TRACE;
            break;
        }
        return PARSE_ERR;

    case 6:
        if (!strncmp(begin, "DELETE", 6)) {
            req->method = DELETE;
            break;
        }
        return PARSE_ERR;

    case 7:
        if (!strncmp(begin, "OPTIONS", 7)) {
            req->method = OPTIONS;
            break;
        } else if (!strncmp(begin, "CONNECT", 7)) {
            req->method = CONNECT;
            break;
        }
        return PARSE_ERR;

    default:
        return PARSE_ERR;
    }

    req->recv_buf.begin = end;
    req->stage = PARSE_URL;
    return PARSE_OK;
}

/* http://<host>:<port>/<path>?<query>#<frag> */
static int parse_url(request_t *req, char *end)
{
    assert(req->stage == PARSE_URL);

}

static int parse_http_version(request_t *req, char *end)
{
    assert(req->stage == PARSE_HTTP_VERSION);

    char *begin = req->recv_buf.begin;
    int len = end - begin;

    if (len != 8)
        return PARSE_ERR;
    /* 只支持http/1.0 1.1 */
    if (strncasecmp(begin, "HTTP/1.", 7))
        return PARSE_ERR;
    req->http_version.major = 1;
    if (*(begin + 7) == '0')
        req->http_version.minor = 0;
    else if (*(begin + 7) == '1')
        req->http_version.minor = 1;
    else
        return PARSE_ERR;
}

static int parse_request_line(request_t *req)
{
    assert(req->stage == PARSE_REQUEST_LINE);
    req->stage = PARSE_METHOD;

    char *p;
    for (p = req->recv_buf.begin; p < req->check_ch; p++) {
        switch (req->stage) {
        case PARSE_METHOD:
            switch (*p) {
            case ' ':
            case '\t':
                if (parse_method(req, p) != PARSE_OK)
                    return PARSE_ERR;
                p = parse_whitespace(req, p);
                break;

            default:
                break;
            }
            break;

        case PARSE_URL:
            switch (*p) {
            case ' ':
            case '\t':
                if (parse_url(req, p) != PARSE_OK)
                    return PARSE_ERR;
                p = parse_whitespace(req, p);
                break;

            default:
                break;
            }
            break;

        case PARSE_HTTP_VERSION:
            switch(*p) {
            case '\r':
                if (parse_http_version(req, p) != PARSE_OK)
                    return PARSE_ERR;
                break;

            case '\n':
                if (p + 1 != req->check_ch)
                    return PARSE_ERR;
                break;

            default:
                break;
            }
            break;

        default:
            MUSE_EXIT_ON(1, "unknown parse stage in parse_request_line");
        }
    }

    req->recv_buf.begin = req->check_ch;
    req->stage = PARSE_HEADER;

    return PARSE_OK;
}

static int parse_header(request_t *req)
{
}

static int parse_body(request_t *req)
{
}

static void build_response(request_t *req)
{
}

int parse_request(request_t *req)
{
    assert(req);

    int ret;
    while ((ret = parse_line(req)) == PARSE_OK) {
        switch (req->stage) {
        case PARSE_REQUEST_LINE:
            ret = parse_request_line(req);
            break;

        case PARSE_HEADER:
            ret = parse_header(req);
            break;

        case PARSE_BODY:
            ret = parse_body(req);
            break;

        default:
            MUSE_EXIT_ON(1, "unknown parse stage of request");
        }
        if (ret == PARSE_ERR)
            break;
    }

    if (ret == PARSE_ERR) {
        build_response(req);
        return PARSE_ERR;
    }
    if (req->stage == PARSE_DONE) {
        build_response(req);
        return PARSE_OK;
    }
    return PARSE_AGAIN;
}
