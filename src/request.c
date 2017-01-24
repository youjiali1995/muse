#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <ctype.h>
#include <string.h>
#include <strings.h>
#include <stdbool.h>
#include <assert.h>
#include "util.h"
#include "request.h"
#include "server.h"

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
    req->status_code = 200;

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
                if (*(req->check_ch + 1) != '\n') {
                    req->status_code = 400;
                    return PARSE_ERR;
                } else {
                    req->check_ch += 2;
                    return PARSE_OK;
                }
            }
            return PARSE_AGAIN;

        case '\n':
            /* request line 不能有\n */
            if (req->stage == PARSE_REQUEST_LINE) {
                req->status_code = 400;
                return PARSE_ERR;
            }
            /* header 可以有\n，不过在新行开始需要有空格或\t */
            if (req->stage == PARSE_HEADER) {
                if (req->check_ch + 1 < req->recv_buf.end) {
                    if (*(req->check_ch + 1) != ' ' && *(req->check_ch + 1) != '\t') {
                        req->status_code = 400;
                        return PARSE_ERR;
                    } else
                        req->check_ch += 2;
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
    for (; p < req->check_ch && (*p == ' ' || *p == '\t'); p++)
        ;
    req->recv_buf.begin = p;
    return p;
}

static int parse_method(request_t *req, char *end)
{
    assert(req->stage == PARSE_METHOD);
    char *begin = req->recv_buf.begin;
    int len = end - begin;

    req->status_code = 400;
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

    /* TODO */
    if (req->method != GET) {
        req->status_code = 501;
        return PARSE_ERR;
    }

    req->status_code = 200;
    req->recv_buf.begin = end;
    req->stage = PARSE_URL;
    return PARSE_OK;
}

static bool is_valid_scheme_ch(char ch)
{
    switch (ch) {
    case '-': /* gsm-sms view-source */
    case '+': /* whois++ */
    case '.': /* z39.50r z39.50s */
        return true;

    default:
        if (isalnum(ch))
            return true;
    }
    return false;
}

static bool is_valid_host_ch(char ch)
{
    switch (ch) {
    case '-':
    case '.':
        return true;

    default:
        if (isalnum(ch))
            return true;
    }
    return false;
}

/* TODO: 转义检查 */
static bool is_valid_path_ch(char ch)
{
    switch (ch) {
    case '%': /* 转义标志 */
    case '/': /* 分隔符 */
    case ';': case '=': /* 参数 */
    case '.': /* extension */
        return true;

    /* 需要转义 */
    case ':':
    case '$':
    case '+':
    case '@':
    case '&':
    case '{':
    case '}':
    case '|':
    case '\\':
    case '^':
    case '~':
    case '[':
    case ']':
    case '\'':
    case '<':
    case '>':
    case '"':
    case ' ':
        return false;

    default:
        if ((0x00 <= ch && ch <= 0x1F) || ch >= 0x7F)
            return false;
    }
    return true;
}

static bool is_valid_query_ch(char ch)
{
    switch (ch) {
    /* 键值对 */
    case '=':
    case '&':
    case '%':
        return true;

    default:
        if (isalnum(ch))
            return true;
    }
    return false;
}

/* TODO: CGI? */
static int handle_path(request_t *req)
{
    char *path = (req->url.path.len == 0) ? "./" : req->url.path.str;
    /* 不允许绝对路径 */
    if (path[0] == '/')
        return PARSE_ERR;

    int fd = openat(server_cfg.root_fd, path, O_RDONLY);
    if (fd == -1) {
        req->status_code = 404;
        return PARSE_ERR;
    }

    struct stat stat;
    fstat(fd, &stat);
    if (S_ISDIR(stat.st_mode)) {
        int dir_fd = fd;
        fd = openat(dir_fd, "index.html", O_RDONLY);
        close(dir_fd);
        if (fd == -1) {
            req->status_code = 404;
            return PARSE_ERR;
        }
        fstat(fd, &stat);
        req->url.extension = STR("html");
    }

    req->resource_fd = fd;
    req->resource_size = stat.st_size;
    return PARSE_OK;
}

/* 完整：<scheme>://<user>:<password>@<host>:<port>/<path>;<params>?<query>#<frag>
 * 支持：[<http>://<host>[:<port>]][/<path>[?<query>]]
 */
static int parse_url(request_t *req, char *end)
{
    assert(req->stage == PARSE_URL);

    req->status_code = 400;
    for (char *p = req->recv_buf.begin; p < end; p++) {
        switch (req->stage) {
        case PARSE_URL:
            switch (*p) {
            case 'h':
            case 'H':
                req->stage = PARSE_URL_SCHEME;
                req->url.scheme.str = p;
                break;

            case '/':
                req->stage = PARSE_URL_PATH;
                req->url.path.str = p + 1;
                break;

            default:
                return PARSE_ERR;
            }
            break;

        case PARSE_URL_SCHEME:
            switch (*p) {
            case ':':
                req->url.scheme.len = p - req->url.scheme.str;
                req->stage = PARSE_URL_HOST;
                req->url.host.str = p + 3;
                /* :// */
                if (p + 2 <= end || *(p + 1) != '/' || *(p + 2) != '/')
                    return PARSE_ERR;
                p += 2;
                break;

            default:
                if (!is_valid_scheme_ch(*p))
                    return PARSE_ERR;
                break;
            }
            break;

        case PARSE_URL_HOST:
            switch (*p) {
            case ':':
                req->url.host.len = p - req->url.host.str;
                req->stage = PARSE_URL_PORT;
                req->url.port.str = p + 1;
                break;

            case '/':
                req->url.host.len = p - req->url.host.str;
                req->stage = PARSE_URL_PATH;
                req->url.path.str = p + 1;
                break;

            default:
                if (!is_valid_host_ch(*p))
                    return PARSE_ERR;
                break;
            }
            break;

        case PARSE_URL_PORT:
            switch (*p) {
            case '/':
                req->url.port.len = p - req->url.port.str;
                req->stage = PARSE_URL_PATH;
                req->url.path.str = p + 1;
                break;

            default:
                if (!isdigit(*p))
                    return PARSE_ERR;
                break;
            }
            break;

        case PARSE_URL_PATH:
            switch (*p) {
            case '.':
                req->url.extension.str = p + 1;
                break;

            case '?':
                req->url.path.len = p - req->url.path.str;
                if (req->url.extension.str)
                    req->url.extension.len = p - req->url.extension.str;
                req->stage = PARSE_URL_QUERY;
                req->url.query.str = p + 1;
                break;

            default:
                if (!is_valid_path_ch(*p))
                    return PARSE_ERR;
                break;
            }
            break;

        case PARSE_URL_QUERY:
            switch (*p) {
            /* TODO */
            default:
                if (!is_valid_query_ch(*p))
                    return PARSE_ERR;
                break;
            }
            break;

        default:
            MUSE_EXIT_ON(0, "unknown stage in parse_url");
        }
    }

    switch (req->stage) {
    case PARSE_URL_HOST:
        assert(req->url.host.str);
        req->url.host.len = end - req->url.host.str;
        if (req->url.host.len == 0)
            return PARSE_ERR;
        break;

    case PARSE_URL_PORT:
        assert(req->url.port.str);
        req->url.port.len = end - req->url.port.str;
        if (req->url.port.len == 0)
            return PARSE_ERR;
        break;

    case PARSE_URL_PATH:
        assert(req->url.path.str);
        req->url.path.len = end - req->url.path.str;
        if (req->url.extension.str)
            req->url.extension.len = end - req->url.extension.str;
        break;

    case PARSE_URL_QUERY:
        assert(req->url.query.str);
        req->url.query.len = end - req->url.query.str;
        if (req->url.query.len == 0)
            return PARSE_ERR;
        break;

    default:
        return PARSE_ERR;
    }

    if (handle_path(req) != PARSE_OK)
        return PARSE_ERR;

    req->status_code = 200;
    req->recv_buf.begin = end;
    req->stage = PARSE_HTTP_VERSION;
    return PARSE_OK;
}

static int parse_http_version(request_t *req, char *end)
{
    assert(req->stage == PARSE_HTTP_VERSION);

    char *p = req->recv_buf.begin;
    int len = end - p;

    req->status_code = 400;
    if (len <= 5 || strncasecmp(p, "HTTP/", 5))
        return PARSE_ERR;
    for (p += 5; p < end && *p != '.'; p++) {
        if (!isdigit(*p))
            return PARSE_ERR;
        req->http_version.major = req->http_version.major * 10 + *p - '0';
    }
    /* HTTP/1 HTTP/1. */
    if (p == end || p + 1 == end)
        return PARSE_ERR;
    for (p += 1; p < end; p++) {
        if (!isdigit(*p))
            return PARSE_ERR;
        req->http_version.minor = req->http_version.minor * 10 + *p - '0';
    }
    /* 只支持http/1.0 1.1 */
    if (req->http_version.major != 1
            || (req->http_version.minor != 0 && req->http_version.minor != 1)) {
        req->status_code = 505;
        return PARSE_ERR;
    }

    req->status_code = 200;
    req->stage = PARSE_HEADER;
    return PARSE_OK;
}

static int parse_request_line(request_t *req)
{
    assert(req->stage == PARSE_REQUEST_LINE);
    req->stage = PARSE_METHOD;

    char *end = req->check_ch;
    for (char *p = req->recv_buf.begin; p < end; p++) {
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
                assert(p + 2 == end && *(p + 1) == '\n');
                if (parse_http_version(req, p) != PARSE_OK)
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

    if (req->stage != PARSE_HEADER) {
        req->status_code = 400;
        return PARSE_ERR;
    }
    req->recv_buf.begin = end;
    return PARSE_OK;
}

static int handle_header(request_t *req, str_t *name, str_t *value)
{
    return PARSE_OK;
}

static int parse_header(request_t *req)
{
    assert(req->stage == PARSE_HEADER);

    char *p = req->recv_buf.begin;
    char *end = req->check_ch;
    /* 可以只检查长度 */
    if (end - p == 2 && *p == '\r' && *(p + 1) == '\n') {
        req->stage = (req->method == PUT || req->method == POST) ? PARSE_BODY : PARSE_DONE;
        return PARSE_OK;
    }

    str_t name, value;
    str_init(&name);
    str_init(&value);

    req->stage = PARSE_HEADER_NAME;
    name.str = p;
    for (; p < end; p++) {
        switch (req->stage) {
        case PARSE_HEADER_NAME:
            switch (*p) {
            case ':':
                name.len = p - name.str;
                req->stage = PARSE_HEADER_VALUE;
                value.str = p = parse_whitespace(req, p);
                break;

            case '-':
                *p = '_';
                break;

            default:
                if (!isalnum(*p)) {
                    req->status_code = 400;
                    return PARSE_ERR;
                }
                if ('A' <= *p && *p <= 'Z')
                    *p = *p - 'A' + 'a';
                break;
            }
            break;

        case PARSE_HEADER_VALUE:
            switch (*p) {
            case '\r':
                assert(p + 2 == end && *(p + 1) == '\n');
                value.len = p - value.str;
                break;

            default:
                break;
            }
            break;

        default:
            MUSE_EXIT_ON(1, "unknown stage in parse_header");
        }
    }

    if (handle_header(req, &name, &value) != PARSE_OK) {
        req->status_code = 400;
        return PARSE_ERR;
    }

    req->stage = PARSE_HEADER;
    req->recv_buf.begin = end;
    return PARSE_OK;
}

static int parse_body(request_t *req)
{
    assert(req->stage == PARSE_BODY);
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
