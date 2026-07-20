#ifndef DIALER_H
#define DIALER_H

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    char     host[256];
    char     port[8]; /* keep the service in getaddrinfo format */
} dialer_ctx_t;

void dialer_set_target(dialer_ctx_t *d, const char *host, int port);

int dialer_connect(void *ctx);

#ifdef __cplusplus
}
#endif

#endif /* dialer_h */
