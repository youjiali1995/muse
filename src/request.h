#ifndef MUSE_REQUEST_H__
#define MUSE_REQUEST_H__

#include "buffer.h"
#include "str.h"

#define PARSE_ERR (-1)
#define PARSE_OK (0)
#define PARSE_AGAIN (1)

typedef enum {
    GET,
    PUT,
    HEAD,
    POST,
    TRACE,
    DELETE,
    CONNECT,
    OPTIONS
} method_t;

typedef struct {
    str_t scheme;
    str_t host;
    str_t port;
    str_t path;
    str_t extension;
    str_t query;
} url_t;

typedef struct {
    int major;
    int minor;
} version_t;

typedef struct {
    /* general headers */
    str_t connection;
    str_t date;
    str_t mime_version;
    str_t trailer;
    str_t transfer_encoding;
    str_t update;
    str_t via;
    str_t cache_control;
    str_t pragma;
    /* request headers */
    str_t client_ip;
    str_t from;
    str_t host;
    str_t referer;
    str_t ua_color;
    str_t ua_cpu;
    str_t ua_disp;
    str_t ua_os;
    str_t ua_pixels;
    str_t user_agent;
    str_t accept;
    str_t accept_charset;
    str_t accept_encoding;
    str_t accept_language;
    str_t te;
    str_t expect;
    str_t if_match;
    str_t if_modified_since;
    str_t if_none_match;
    str_t if_range;
    str_t if_unmodified_since;
    str_t range;
    str_t authorization;
    str_t cookie;
    str_t cookie2;
    str_t max_forward;
    str_t proxy_authorization;
    str_t proxy_connection;
    /* entity headers */
    str_t allow;
    str_t location;
    str_t content_base;
    str_t content_encoding;
    str_t content_language;
    str_t content_length;
    str_t content_location;
    str_t content_md5;
    str_t content_range;
    str_t content_type;
    str_t etag;
    str_t expires;
    str_t last_modified;
    /* chrome */
    str_t upgrade_insecure_requests;
} header_t;

typedef enum {
    PARSE_REQUEST_LINE,
    PARSE_METHOD,
    PARSE_URL,
    PARSE_URL_SCHEME,
    PARSE_URL_HOST,
    PARSE_URL_PORT,
    PARSE_URL_PATH,
    PARSE_URL_QUERY,
    PARSE_HTTP_VERSION,

    PARSE_HEADER,
    PARSE_HEADER_NAME,
    PARSE_HEADER_VALUE,

    PARSE_BODY,
    PARSE_DONE
} parse_stage_t;

typedef struct {
    /* request line */
    method_t method;
    url_t url;
    version_t http_version;

    header_t header;
    int content_length;
    str_t body;

    parse_stage_t stage;
    char *check_ch;
    int status_code;

    buffer_t recv_buf;
    buffer_t send_buf;
    int resource_fd;
    int resource_size;
} request_t;

#define request_clear(req) request_init(req)

void header_init(void);
void mime_init(void);
void request_init(request_t *req);
int parse_request(request_t *req);

#endif
