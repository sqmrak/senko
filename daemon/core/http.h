#ifndef HTTP_H
#define HTTP_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    HTTP_FRAMING_UNKNOWN = 0, /* wait for headers */
    HTTP_FRAMING_LENGTH, /* use content length */
    HTTP_FRAMING_CHUNKED, /* read chunks */
    HTTP_FRAMING_EOF /* read until close */
} http_framing_t;

typedef enum {
    HTTP_NEED_MORE =  1, /* keep reading */
    HTTP_DONE      =  0, /* response is complete */
    HTTP_ERR_ARG   = -1,
    HTTP_ERR_PARSE = -2, /* reject bad framing */
    HTTP_ERR_TOOBIG= -3, /* cap response data */
    HTTP_ERR_STATUS= -4 /* reject bad status */
} http_status_t;

typedef enum {
    HP_STATUS_LINE = 0, /* read the status */
    HP_HEADERS, /* read the headers */
    HP_BODY_LENGTH, /* read the body length */
    HP_BODY_CHUNK_SIZE, /* read the chunk size */
    HP_BODY_CHUNK_DATA, /* read the chunk */
    HP_BODY_CHUNK_CRLF, /* read the chunk ending */
    HP_BODY_EOF, /* read until close */
    HP_COMPLETE, /* stop reading */
    HP_ERROR
} http_parse_state_t;

#define HTTP_MAX_HEADER   (8 * 1024) /* cap header buffering */
#define HTTP_MAX_LOCATION 1024

typedef struct {
    http_parse_state_t state;

    int            status_code; /* response code */
    http_framing_t framing;

    char    line[HTTP_MAX_HEADER];
    size_t  line_len;
    size_t  header_total; /* header bytes read */

    size_t  content_length;
    size_t  body_got; /* body bytes read */

    size_t  chunk_remaining; /* active chunk size */

    char    location[HTTP_MAX_LOCATION];
    int     have_location;

/* keep cookies across redirects */
    char    set_cookie[512];
    int     have_set_cookie;

    char    subscription_userinfo[512];
    int     have_subscription_userinfo;

    uint8_t *body;
    size_t   body_cap;
    size_t   body_len;
} http_parser_t;

void http_parser_init(http_parser_t *p, uint8_t *body_buf, size_t body_cap);

/* feed response bytes */
http_status_t http_parser_feed(http_parser_t *p, const uint8_t *in, size_t len);

/* finish an eof-framed response */
http_status_t http_parser_eof(http_parser_t *p);

int http_parser_is_redirect(const http_parser_t *p);

#ifdef __cplusplus
}
#endif

#endif /* http_h */
