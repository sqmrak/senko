#ifndef HTTP_H
#define HTTP_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    HTTP_FRAMING_UNKNOWN = 0, /* wait for headers before choosing framing */
    HTTP_FRAMING_LENGTH, /* trust the declared body length */
    HTTP_FRAMING_CHUNKED, /* decode transfer chunks before delivery */
    HTTP_FRAMING_EOF /* use close as the body boundary */
} http_framing_t;

typedef enum {
    HTTP_NEED_MORE =  1, /* retain state until another read completes it */
    HTTP_DONE      =  0, /* body or redirect is complete */
    HTTP_ERR_ARG   = -1,
    HTTP_ERR_PARSE = -2, /* reject malformed response framing */
    HTTP_ERR_TOOBIG= -3, /* reject data beyond fixed storage */
    HTTP_ERR_STATUS= -4 /* reject non-success non-redirect status */
} http_status_t;

typedef enum {
    HP_STATUS_LINE = 0, /* wait for the status before parsing headers */
    HP_HEADERS, /* wait for the blank line before the body */
    HP_BODY_LENGTH, /* consume exactly the declared length */
    HP_BODY_CHUNK_SIZE, /* parse the next chunk boundary */
    HP_BODY_CHUNK_DATA, /* consume the current chunk */
    HP_BODY_CHUNK_CRLF, /* consume the chunk terminator */
    HP_BODY_EOF, /* consume until the peer closes */
    HP_COMPLETE, /* stop after the body is complete */
    HP_ERROR
} http_parse_state_t;

#define HTTP_MAX_HEADER   (8 * 1024) /* bound header buffering */
#define HTTP_MAX_LOCATION 1024

typedef struct {
    http_parse_state_t state;

    int            status_code; /* e.g. 200, 301 */
    http_framing_t framing;

    char    line[HTTP_MAX_HEADER];
    size_t  line_len;
    size_t  header_total; /* stop unbounded header growth */

    size_t  content_length;
    size_t  body_got; /* track delivered body bytes */

    size_t  chunk_remaining; /* track the active chunk boundary */

    char    location[HTTP_MAX_LOCATION];
    int     have_location;

/* retain cookies so redirects can reuse the session */
    char    set_cookie[512];
    int     have_set_cookie;

    uint8_t *body;
    size_t   body_cap;
    size_t   body_len;
} http_parser_t;

void http_parser_init(http_parser_t *p, uint8_t *body_buf, size_t body_cap);

/* feed bytes until framing completes or rejects the response */
http_status_t http_parser_feed(http_parser_t *p, const uint8_t *in, size_t len);

/* treat close as success only when eof framing selected it as the boundary */
http_status_t http_parser_eof(http_parser_t *p);

int http_parser_is_redirect(const http_parser_t *p);

#ifdef __cplusplus
}
#endif

#endif /* http_h */
