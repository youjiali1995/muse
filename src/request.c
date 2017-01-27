#define _POSIX_C_SOURCE 200809L /* for openat */
#include <time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <ctype.h>
#include <string.h>
#include <strings.h>
#include <stdbool.h>
#include <assert.h>
#include <stddef.h>
#include "util.h"
#include "request.h"
#include "server.h"
#include "dict.h"

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
    req->content_length = 0;
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

/* TODO: path检查.. */
static int handle_path(request_t *req)
{
    char *path = (req->url.path.len == 0) ? "./" : req->url.path.str;
    if (path[0] == '/')
        return PARSE_ERR;

    int fd = openat(server_cfg.src_root, path, O_RDONLY);
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
            /* TODO: bug */
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
            MUSE_EXIT_ON(1, "invalid stage in parse_url");
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
            MUSE_EXIT_ON(1, "invalid parse stage in parse_request_line");
        }
    }

    if (req->stage != PARSE_HEADER) {
        req->status_code = 400;
        return PARSE_ERR;
    }
    req->recv_buf.begin = end;
    return PARSE_OK;
}

/* TODO */
static int handle_generic_header(request_t *req, size_t offset, str_t *value)
{
    str_t *header = (str_t *) (((char *) &req->header) + offset);
    *header = *value;
    return PARSE_OK;
}

static int handle_content_length(request_t *req, size_t offset, str_t *value)
{
    handle_generic_header(req, offset, value);
    for (int i = 0; i < value->len; i++) {
        if (!isdigit(value->str[i])) {
            req->status_code = 400;
            return PARSE_ERR;
        }
        req->content_length = req->content_length * 10 + value->str[i] - '0';
    }
    return PARSE_OK;
}

typedef struct {
    size_t offset;
    int (*header_processor)(request_t *req, size_t offset, str_t *value);
} header_handler_t;

typedef struct {
    str_t header;
    header_handler_t handler;
} header_map_t;

/* 需要持久存储。两种方法，一是静态或全局分配，二是动态分配，动态分配太麻烦。
 * 但是全局变量初始化只能使用常量，也就是编译时确定的，所以只能一个一个赋值，
 * 这里讨巧了，先将header长度设长1， 后面再减。
 */
#define HEADER_MAP(header, processor) \
    {{#header, sizeof(#header)}, {offsetof(header_t, header), processor}}

static header_map_t _headers[] = {
    HEADER_MAP(connection, handle_generic_header),
    HEADER_MAP(date, handle_generic_header),
    HEADER_MAP(mime_version, handle_generic_header),
    HEADER_MAP(trailer, handle_generic_header),
    HEADER_MAP(transfer_encoding, handle_generic_header),
    HEADER_MAP(update, handle_generic_header),
    HEADER_MAP(via, handle_generic_header),
    HEADER_MAP(cache_control, handle_generic_header),
    HEADER_MAP(pragma, handle_generic_header),
    HEADER_MAP(client_ip, handle_generic_header),
    HEADER_MAP(from, handle_generic_header),
    HEADER_MAP(host, handle_generic_header),
    HEADER_MAP(referer, handle_generic_header),
    HEADER_MAP(ua_color, handle_generic_header),
    HEADER_MAP(ua_cpu, handle_generic_header),
    HEADER_MAP(ua_disp, handle_generic_header),
    HEADER_MAP(ua_os, handle_generic_header),
    HEADER_MAP(ua_pixels, handle_generic_header),
    HEADER_MAP(user_agent, handle_generic_header),
    HEADER_MAP(accept, handle_generic_header),
    HEADER_MAP(accept_charset, handle_generic_header),
    HEADER_MAP(accept_encoding, handle_generic_header),
    HEADER_MAP(accept_language, handle_generic_header),
    HEADER_MAP(te, handle_generic_header),
    HEADER_MAP(expect, handle_generic_header),
    HEADER_MAP(if_match, handle_generic_header),
    HEADER_MAP(if_modified_since, handle_generic_header),
    HEADER_MAP(if_none_match, handle_generic_header),
    HEADER_MAP(if_range, handle_generic_header),
    HEADER_MAP(if_unmodified_since, handle_generic_header),
    HEADER_MAP(range, handle_generic_header),
    HEADER_MAP(authorization, handle_generic_header),
    HEADER_MAP(cookie, handle_generic_header),
    HEADER_MAP(cookie2, handle_generic_header),
    HEADER_MAP(max_forward, handle_generic_header),
    HEADER_MAP(proxy_authorization, handle_generic_header),
    HEADER_MAP(proxy_connection, handle_generic_header),
    HEADER_MAP(allow, handle_generic_header),
    HEADER_MAP(location, handle_generic_header),
    HEADER_MAP(content_base, handle_generic_header),
    HEADER_MAP(content_encoding, handle_generic_header),
    HEADER_MAP(content_language, handle_generic_header),
    HEADER_MAP(content_length, handle_content_length),
    HEADER_MAP(content_location, handle_generic_header),
    HEADER_MAP(content_md5, handle_generic_header),
    HEADER_MAP(content_range, handle_generic_header),
    HEADER_MAP(content_type, handle_generic_header),
    HEADER_MAP(etag, handle_generic_header),
    HEADER_MAP(expires, handle_generic_header),
    HEADER_MAP(last_modified, handle_generic_header)
};

static dict_t *header_handlers;

void header_init(void)
{
    header_handlers = dict_new();
    MUSE_EXIT_ON(!header_handlers, "header_init failed");

    int n = sizeof(_headers) / sizeof(_headers[0]);
    for (int i = 0; i < n; i++) {
        _headers[i].header.len -= 1;
        dict_put(header_handlers, &_headers[i].header, &_headers[i].handler);
    }
}

static int handle_header(request_t *req, str_t *name, str_t *value)
{
    header_handler_t *handler = dict_get(header_handlers, name);
    if (!handler || value->len == 0) {
        req->status_code = 400;
        return PARSE_ERR;
    }
    return handler->header_processor(req, handler->offset, value);
}

static int parse_header(request_t *req)
{
    assert(req->stage == PARSE_HEADER);

    char *p = req->recv_buf.begin;
    char *end = req->check_ch;
    /* 可以只检查长度 */
    if (end - p == 2 && *p == '\r' && *(p + 1) == '\n') {
        /* HTTP1.1 客户端必须在所有请求中包含Host首部 */
        if (req->http_version.major == 1 && req->http_version.minor == 1
                && req->header.host.len == 0) {
            req->status_code = 400;
            return PARSE_ERR;
        }
        req->recv_buf.begin = end;
        req->stage = PARSE_BODY;
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
            MUSE_EXIT_ON(1, "invalid stage in parse_header");
        }
    }

    if (handle_header(req, &name, &value) != PARSE_OK)
        return PARSE_ERR;

    req->stage = PARSE_HEADER;
    req->recv_buf.begin = end;
    return PARSE_OK;
}

static int parse_body(request_t *req)
{
    assert(req->stage == PARSE_BODY);

    switch (req->method) {
    case PUT:
    case POST:
        if (req->content_length == 0) {
            req->status_code = 400;
            return PARSE_ERR;
        }
        break;

    default:
        if (req->content_length != 0) {
            req->status_code = 400;
            return PARSE_ERR;
        }
        req->stage = PARSE_DONE;
        return PARSE_OK;
    }

    if (buffer_size(&req->recv_buf) < req->content_length)
        return PARSE_AGAIN;
    req->body.str = req->recv_buf.begin;
    req->body.len = req->content_length;
    req->stage = PARSE_DONE;
    req->recv_buf.begin += req->content_length;
    return PARSE_OK;
}

#define MIME_MAP(extension, mime_type) \
    {{#extension, sizeof(#extension)}, {#mime_type, sizeof(#mime_type)}}

static str_t _mimes[][2] = {
    MIME_MAP(htm, text/html),
    MIME_MAP(html, text/html),
    MIME_MAP(txt, text/plain),
    MIME_MAP(css, text/css),

    MIME_MAP(jpeg, image/jepg),
    MIME_MAP(jpg, image/jepg),
    MIME_MAP(png, image/png),
    MIME_MAP(gif, image/gif),
    MIME_MAP(ico, image/x-icon)
};

static dict_t *mime_maps;

void mime_init(void)
{
    mime_maps = dict_new();
    MUSE_EXIT_ON(!mime_maps, "mime_init failed");

    int n = sizeof(_mimes) / sizeof(_mimes[0]);
    for (int i = 0; i < n; i++) {
        _mimes[i][0].len -= 1;
        _mimes[i][1].len -= 1;
        dict_put(mime_maps, &_mimes[i][0], &_mimes[i][1]);
    }
}

static const str_t status_reason(int status_code)
{
    switch (status_code) {
    case 100:
        return STR("100 Continue");
    case 101:
        return STR("101 Switching Protocols");
    case 200:
        return STR("200 OK");
    case 201:
        return STR("201 Created");
    case 202:
        return STR("202 Accepted");
    case 203:
        return STR("203 Non-Authoritative Information");
    case 204:
        return STR("204 No Content");
    case 205:
        return STR("205 Reset Content");
    case 206:
        return STR("206 Partial Content");
    case 300:
        return STR("300 Multiple Choices");
    case 301:
        return STR("301 Moved Permanently");
    case 302:
        return STR("302 Found");
    case 303:
        return STR("303 See Other");
    case 304:
        return STR("304 Not Modified");
    case 305:
        return STR("305 Use Proxy");
    case 307:
        return STR("307 Temporary Redirect");
    case 400:
        return STR("400 Bad request");
    case 401:
        return STR("401 Unauthorized");
    case 402:
        return STR("402 Payment Required");
    case 403:
        return STR("403 Forbidden");
    case 404:
        return STR("404 Not Found");
    case 405:
        return STR("405 Method Not Allowed");
    case 406:
        return STR("406 Not Acceptable");
    case 407:
        return STR("407 Proxy Authentication Required");
    case 408:
        return STR("408 Request Timeout");
    case 409:
        return STR("409 Conflict");
    case 410:
        return STR("410 Gone");
    case 411:
        return STR("411 Length Required");
    case 412:
        return STR("412 Precondition Failed");
    case 413:
        return STR("413 Request Entity Too Large");
    case 414:
        return STR("414 Request URI Too Long");
    case 415:
        return STR("415 Unsupported Media Type");
    case 416:
        return STR("416 Requested Range Not Satisfiable");
    case 417:
        return STR("417 Expectation Failed");
    case 500:
        return STR("500 Internal Server Error");
    case 501:
        return STR("501 Not Implemented");
    case 502:
        return STR("502 Bad Gateway");
    case 503:
        return STR("503 Service Unavailable");
    case 504:
        return STR("504 Gateway Timeout");
    case 505:
        return STR("505 HTTP Version Not Supported");

    default:
        MUSE_EXIT_ON(1, "invalid http status code");
    }
}

static void build_response_status_line(request_t *req)
{
    buffer_printf(&req->send_buf, "HTTP/1.%d ", req->http_version.minor);
    str_t reason = status_reason(req->status_code);
    buffer_append_str(&req->send_buf, &reason);
    buffer_append_cstr(&req->send_buf, "\r\n");
}

static void build_response_date(request_t *req)
{
    time_t t = time(NULL);
    struct tm *tm = localtime(&t);
    req->send_buf.end += strftime(req->send_buf.end, buffer_space(&req->send_buf),
            "Date: %a, %d %d %Y %H:%M:%S GMT\r\n", tm);
}

#define ERR_FILE(status) #status ".html"

static void build_response_err(request_t *req)
{
    assert(req->status_code >= 300);

    build_response_status_line(req);
    buffer_append_cstr(&req->send_buf, "Server: muse\r\n");
    build_response_date(req);
    buffer_append_cstr(&req->send_buf, "Connection: close\r\n");
    buffer_append_cstr(&req->send_buf, "Content-Type: text/html\r\n");

    if (req->resource_fd != -1)
        close(req->resource_fd);
    req->resource_fd = openat(server_cfg.err_root, ERR_FILE(req->status_code), O_RDONLY);
    MUSE_EXIT_ON(req->resource_fd == -1, ERR_FILE(req->status_code) "not exist");
    struct stat stat;
    fstat(req->resource_fd, &stat);
    req->resource_size = stat.st_size;
    buffer_printf(&req->send_buf, "Content-Length: %d\r\n", req->resource_size);

    buffer_append_cstr(&req->send_buf, "\r\n");
}

static void build_response_ok(request_t *req)
{
    switch (req->status_code) {
    case 100:
    case 101:
    case 201:
    case 202:
    case 203:
    case 204:
    case 205:
    case 206:
        /* TODO */
        break;

    case 200:
        break;

    default:
        MUSE_EXIT_ON(1, "invalid status code in build_response_ok");
    }

    build_response_status_line(req);
    buffer_append_cstr(&req->send_buf, "server: muse\r\n");
    build_response_date(req);

    buffer_append_cstr(&req->send_buf, "Content-type: ");
    str_t *mime_type = dict_get(mime_maps, &req->url.extension);
    if (mime_type)
        buffer_append_str(&req->send_buf, mime_type);
    else
        buffer_append_cstr(&req->send_buf, "text/html");
    buffer_append_cstr(&req->send_buf, "\r\n");
    buffer_printf(&req->send_buf, "Content-Length: %d\r\n", req->resource_size);

    buffer_append_cstr(&req->send_buf, "\r\n");
}

int parse_request(request_t *req)
{
    assert(req);

    int ret;
    while (req->stage != PARSE_BODY && ((ret = parse_line(req)) == PARSE_OK)) {
        switch (req->stage) {
        case PARSE_REQUEST_LINE:
            ret = parse_request_line(req);
            break;

        case PARSE_HEADER:
            ret = parse_header(req);
            break;

        default:
            MUSE_EXIT_ON(1, "invalid parse stage of request");
        }
        if (ret == PARSE_ERR)
            break;
    }

    if (req->stage == PARSE_BODY)
        ret = parse_body(req);

    if (ret == PARSE_ERR) {
        build_response_err(req);
        return PARSE_ERR;
    }
    if (req->stage == PARSE_DONE) {
        build_response_ok(req);
        return PARSE_OK;
    }
    return PARSE_AGAIN;
}
