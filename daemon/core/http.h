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

/* takes the body as it arrives instead of the parser keeping it; non-zero
   aborts the parse. a body that does not fit memory goes straight to a file */
typedef int (*http_body_sink_fn)(void *ctx, const uint8_t *data, size_t len);

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

/* a happ routing profile rides in one response header, and its base64 form runs
   to a few kilobytes, so a header line may be this long. a panel's other headers
   stay far below it */
#define HTTP_MAX_HEADER   (24 * 1024) /* cap header buffering */
#define HTTP_ROUTING_MAX  (20 * 1024)
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
/* profile-title is the panel's suggested name for the subscription itself,
   not prose about it; subscription-description is the rare, separate header
   that actually is prose. conflating the two left the suggested name stranded
   on a details screen instead of ever becoming the subscription's name */
    char    subscription_title[256];
    int     have_subscription_title;
    char    subscription_description[256];
    int     have_subscription_description;
    char    subscription_support_url[512];
    int     have_subscription_support_url;
/* the routing header, whole or not at all: a cut base64 profile would decode to
   a different, wrong profile */
    char    routing[HTTP_ROUTING_MAX];
    int     have_routing;
    int     routing_too_long;

/* remnawave device gating: instead of an http error the panel answers with a
   one entry placeholder profile, so the reason has to come from the headers */
    int     hwid_rejected;
    char    announce[512];
    int     have_announce;

    uint8_t *body;
    size_t   body_cap;
    size_t   body_len;
/* when set, body bytes go to the sink and body_cap is the most it may take */
    http_body_sink_fn sink;
    void    *sink_ctx;
/* set with HTTP_ERR_TOOBIG once the body filled body_cap; the buffer then
   holds the first body_cap bytes */
    int      body_cut;
} http_parser_t;

void http_parser_init(http_parser_t *p, uint8_t *body_buf, size_t body_cap);

/* bytes of a 2xx body go to fn; body_cap of http_parser_init is then the limit
   on their total, and the body buffer is not used */
void http_parser_set_sink(http_parser_t *p, http_body_sink_fn fn, void *ctx);

http_status_t http_parser_feed(http_parser_t *p, const uint8_t *in, size_t len);

http_status_t http_parser_eof(http_parser_t *p);

int http_parser_is_redirect(const http_parser_t *p);

#ifdef __cplusplus
}
#endif

#endif /* http_h */
