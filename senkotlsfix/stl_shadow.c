#define _DEFAULT_SOURCE

#include "stl_gate.h"
#include "stl_log.h"
#include "fishhook.h"

#include <Security/Security.h>
#include <Security/SecureTransport.h>
#include <CoreFoundation/CoreFoundation.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "mbedtls/ssl.h"
#include "mbedtls/entropy.h"
#include "mbedtls/ctr_drbg.h"
#include "mbedtls/error.h"
#include "mbedtls/x509_crt.h"
#include "psa/crypto.h"

#ifndef MBEDTLS_ERR_NET_RECV_FAILED
#define MBEDTLS_ERR_NET_RECV_FAILED -0x004C
#endif
#ifndef MBEDTLS_ERR_NET_SEND_FAILED
#define MBEDTLS_ERR_NET_SEND_FAILED -0x004E
#endif

#ifndef errSSLWouldBlock
#define errSSLWouldBlock -9803
#endif
#define ST_ClosedGraceful -9805
#define ST_ClosedAbort    -9806
#define ST_Connected       2
#define ST_TLS12           8

#define stl_ca_path "/usr/lib/senkotlsfix/cacert.pem"
#define MAXSH 256
#define DRAIN_MAX 64

typedef struct {
    SSLContextRef    ctx;
    SSLReadFunc      rf;
    SSLWriteFunc     wf;
    SSLConnectionRef conn;
    char             host[256];
    int              inited;
    int              state;      /* 0 none, 1 handshaking, 2 connected, -1 bypass */
    int              client_cert;
    unsigned         last_use;
    mbedtls_ssl_context ssl;
    mbedtls_ssl_config  conf;
} stl_shadow_t;

static stl_shadow_t *g_tab[MAXSH];
static unsigned g_clock;
static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;

static mbedtls_ctr_drbg_context g_drbg;
static mbedtls_entropy_context  g_entropy;
static int g_drbg_ready;
static pthread_mutex_t g_rng = PTHREAD_MUTEX_INITIALIZER;

static mbedtls_x509_crt g_ca;
static int g_ca_ok;

static int g_allow_tls13 = 1;
static int g_drain_guard = 1;
static int g_sys_fallback = 1;

static CFMutableSetRef g_trust_set;
static void *g_trust_ring[64];
static int g_trust_idx;
static pthread_mutex_t g_trust_lock = PTHREAD_MUTEX_INITIALIZER;

/* short-lived host blacklist only for permanent mbed_init failures.
   never poison hosts for wire blips: that forced safari double-reloads*/
static char g_fail_hosts[32][256];
static unsigned g_fail_ts[32];
static int g_fail_idx;
static pthread_mutex_t g_fail_lock = PTHREAD_MUTEX_INITIALIZER;
#define STL_FAIL_TTL_SEC 45

static int g_ready;
static pthread_once_t g_once = PTHREAD_ONCE_INIT;

static unsigned stl_now_sec(void) {
    return (unsigned)time(NULL);
}

static int stl_rng_cb(void *p, unsigned char *out, size_t len) {
    (void)p;
    pthread_mutex_lock(&g_rng);
    int rc = mbedtls_ctr_drbg_random(&g_drbg, out, len);
    pthread_mutex_unlock(&g_rng);
    return rc;
}

static void stl_trust_remember(void *t) {
    pthread_mutex_lock(&g_trust_lock);
    void *old = g_trust_ring[g_trust_idx % 64];
    if (old && g_trust_set) CFSetRemoveValue(g_trust_set, old);
    g_trust_ring[g_trust_idx % 64] = t;
    g_trust_idx++;
    if (g_trust_set) CFSetAddValue(g_trust_set, t);
    pthread_mutex_unlock(&g_trust_lock);
}

int stl_shadow_trust_is_mine(void *t) {
    if (!g_trust_set || !t) return 0;
    pthread_mutex_lock(&g_trust_lock);
    int f = CFSetContainsValue(g_trust_set, t);
    pthread_mutex_unlock(&g_trust_lock);
    return f;
}

static int stl_host_is_failed(const char *h) {
    if (!h || !h[0]) return 0;
    unsigned now = stl_now_sec();
    int f = 0;
    pthread_mutex_lock(&g_fail_lock);
    for (int i = 0; i < 32; i++) {
        if (!g_fail_hosts[i][0]) continue;
        if (now - g_fail_ts[i] > STL_FAIL_TTL_SEC) {
            g_fail_hosts[i][0] = '\0';
            continue;
        }
        if (strcmp(g_fail_hosts[i], h) == 0) { f = 1; break; }
    }
    pthread_mutex_unlock(&g_fail_lock);
    return f;
}

static void stl_host_mark_failed(const char *h) {
    if (!h || !h[0] || stl_host_is_failed(h)) return;
    pthread_mutex_lock(&g_fail_lock);
    int i = g_fail_idx % 32;
    strncpy(g_fail_hosts[i], h, 255);
    g_fail_hosts[i][255] = '\0';
    g_fail_ts[i] = stl_now_sec();
    g_fail_idx++;
    pthread_mutex_unlock(&g_fail_lock);
}

static void stl_shadow_reset_mbed(stl_shadow_t *s) {
    if (!s) return;
    if (s->inited) {
        mbedtls_ssl_free(&s->ssl);
        mbedtls_ssl_config_free(&s->conf);
        s->inited = 0;
    }
    if (s->state != -1) s->state = 0;
}

static void stl_shadow_destroy(stl_shadow_t *s) {
    if (!s) return;
    stl_shadow_reset_mbed(s);
    free(s);
}

static stl_shadow_t *stl_shadow_get(SSLContextRef c) {
    if (!stl_gate_is_active() || !g_ready) return NULL;
    stl_shadow_t *r = NULL;
    pthread_mutex_lock(&g_lock);
    for (int i = 0; i < MAXSH; i++)
        if (g_tab[i] && g_tab[i]->ctx == c) {
            r = g_tab[i];
            r->last_use = ++g_clock;
            break;
        }
    pthread_mutex_unlock(&g_lock);
    return r;
}

static stl_shadow_t *stl_shadow_create(SSLContextRef c) {
    stl_shadow_t *s = stl_shadow_get(c);
    if (s) return s;
    s = (stl_shadow_t *)calloc(1, sizeof(stl_shadow_t));
    if (!s) return NULL;
    s->ctx = c;
    stl_shadow_t *evicted = NULL;
    pthread_mutex_lock(&g_lock);
    int slot = -1;
    for (int i = 0; i < MAXSH; i++)
        if (!g_tab[i]) { slot = i; break; }
    if (slot < 0) {
        /* never evict live handshakes / connected shadows */
        int lru = -1;
        for (int i = 0; i < MAXSH; i++) {
            if (!g_tab[i]) continue;
            if (g_tab[i]->state == 1 || g_tab[i]->state == 2) continue;
            if (lru < 0 || g_tab[i]->last_use < g_tab[lru]->last_use) lru = i;
        }
        if (lru < 0) {
            pthread_mutex_unlock(&g_lock);
            free(s);
            return NULL; /* all slots busy: fall back to system for this ctx */
        }
        evicted = g_tab[lru];
        slot = lru;
    }
    s->last_use = ++g_clock;
    g_tab[slot] = s;
    pthread_mutex_unlock(&g_lock);
    if (evicted) stl_shadow_destroy(evicted);
    return s;
}

static void stl_shadow_free(SSLContextRef c) {
    if (!g_ready) return;
    stl_shadow_t *s = NULL;
    pthread_mutex_lock(&g_lock);
    for (int i = 0; i < MAXSH; i++)
        if (g_tab[i] && g_tab[i]->ctx == c) {
            s = g_tab[i];
            g_tab[i] = NULL;
            break;
        }
    pthread_mutex_unlock(&g_lock);
    stl_shadow_destroy(s);
}

static int stl_bio_send(void *p, const unsigned char *buf, size_t len) {
    stl_shadow_t *s = (stl_shadow_t *)p;
    if (!s->wf || !s->conn) return MBEDTLS_ERR_NET_SEND_FAILED;
    size_t n = len;
    OSStatus os = s->wf(s->conn, buf, &n);
    if (n > 0) return (int)n;
    /* map cfnetwork's zero-byte eagain result to retry */
    if (os == errSSLWouldBlock || os == noErr) return MBEDTLS_ERR_SSL_WANT_WRITE;
    if (os == ST_ClosedGraceful) return MBEDTLS_ERR_SSL_CONN_EOF;
    return MBEDTLS_ERR_NET_SEND_FAILED;
}

static int stl_bio_recv(void *p, unsigned char *buf, size_t len) {
    stl_shadow_t *s = (stl_shadow_t *)p;
    if (!s->rf || !s->conn) return MBEDTLS_ERR_NET_RECV_FAILED;
    size_t n = len;
    OSStatus os = s->rf(s->conn, buf, &n);
    if (n > 0) return (int)n;
    if (os == errSSLWouldBlock || os == noErr) return MBEDTLS_ERR_SSL_WANT_READ;
    if (os == ST_ClosedGraceful) return 0;
    /* surface proxy closes as eof so mbedtls tears down cleanly */
    if (os == ST_ClosedAbort) return MBEDTLS_ERR_SSL_CONN_EOF;
    return MBEDTLS_ERR_NET_RECV_FAILED;
}

static int stl_mbed_init(stl_shadow_t *s) {
    mbedtls_ssl_init(&s->ssl);
    mbedtls_ssl_config_init(&s->conf);
    int ret = mbedtls_ssl_config_defaults(&s->conf, MBEDTLS_SSL_IS_CLIENT,
                                          MBEDTLS_SSL_TRANSPORT_STREAM,
                                          MBEDTLS_SSL_PRESET_DEFAULT);
    if (ret) return ret;
    if (!g_ca_ok)
        return MBEDTLS_ERR_SSL_CA_CHAIN_REQUIRED;
    mbedtls_ssl_conf_ca_chain(&s->conf, &g_ca, NULL);
    mbedtls_ssl_conf_authmode(&s->conf, MBEDTLS_SSL_VERIFY_REQUIRED);
    mbedtls_ssl_conf_rng(&s->conf, stl_rng_cb, NULL);
    mbedtls_ssl_conf_min_tls_version(&s->conf, MBEDTLS_SSL_VERSION_TLS1_2);
    mbedtls_ssl_conf_max_tls_version(&s->conf,
        g_allow_tls13 ? MBEDTLS_SSL_VERSION_TLS1_3 : MBEDTLS_SSL_VERSION_TLS1_2);
    if ((ret = mbedtls_ssl_setup(&s->ssl, &s->conf))) return ret;
    if (s->host[0]) mbedtls_ssl_set_hostname(&s->ssl, s->host);
    mbedtls_ssl_set_bio(&s->ssl, s, stl_bio_send, stl_bio_recv, NULL);
    s->inited = 1;
    return 0;
}

static CFArrayRef stl_cert_array(stl_shadow_t *s) {
    const mbedtls_x509_crt *crt = mbedtls_ssl_get_peer_cert(&s->ssl);
    if (!crt) return NULL;
    CFMutableArrayRef arr = CFArrayCreateMutable(kCFAllocatorDefault, 0, &kCFTypeArrayCallBacks);
    if (!arr) return NULL;
    for (const mbedtls_x509_crt *p = crt; p; p = p->next) {
        CFDataRef d = CFDataCreate(kCFAllocatorDefault, p->raw.p, (CFIndex)p->raw.len);
        SecCertificateRef sc = d ? SecCertificateCreateWithData(kCFAllocatorDefault, d) : NULL;
        if (sc) {
            CFArrayAppendValue(arr, sc);
            CFRelease(sc);
        }
        if (d) CFRelease(d);
    }
    return arr;
}

static int stl_build_trust(stl_shadow_t *s, SecTrustRef *trust) {
    CFArrayRef arr = stl_cert_array(s);
    if (!arr) return 0;
    CFStringRef host_str = s->host[0]
        ? CFStringCreateWithCString(kCFAllocatorDefault, s->host, kCFStringEncodingUTF8)
        : NULL;
    SecPolicyRef pol = SecPolicyCreateSSL(true, host_str);
    if (host_str) CFRelease(host_str);
    SecTrustRef t = NULL;
    OSStatus r = SecTrustCreateWithCertificates(arr, pol, &t);
    if (pol) CFRelease(pol);
    CFRelease(arr);
    if (r == errSecSuccess) {
        stl_trust_remember(t);
        *trust = t;
        return 1;
    }
    return 0;
}

static void stl_ensure_ready(void);

/* hooks stay together because they share shadow state */

static OSStatus (*orig_SSLSetIOFuncs)(SSLContextRef, SSLReadFunc, SSLWriteFunc);
static OSStatus stl_SSLSetIOFuncs(SSLContextRef c, SSLReadFunc rf, SSLWriteFunc wf) {
    if (!stl_gate_is_active()) return orig_SSLSetIOFuncs(c, rf, wf);
    stl_ensure_ready();
    OSStatus r = orig_SSLSetIOFuncs(c, rf, wf);
    stl_shadow_t *s = stl_shadow_create(c);
    if (s) { s->rf = rf; s->wf = wf; }
    return r;
}

static OSStatus (*orig_SSLSetConnection)(SSLContextRef, SSLConnectionRef);
static OSStatus stl_SSLSetConnection(SSLContextRef c, SSLConnectionRef conn) {
    if (!stl_gate_is_active()) return orig_SSLSetConnection(c, conn);
    stl_ensure_ready();
    OSStatus r = orig_SSLSetConnection(c, conn);
    stl_shadow_t *s = stl_shadow_create(c);
    if (s) s->conn = conn;
    return r;
}

static OSStatus (*orig_SSLSetPeerDomainName)(SSLContextRef, const char *, size_t);
static OSStatus stl_SSLSetPeerDomainName(SSLContextRef c, const char *name, size_t len) {
    if (!stl_gate_is_active()) return orig_SSLSetPeerDomainName(c, name, len);
    stl_ensure_ready();
    OSStatus r = orig_SSLSetPeerDomainName(c, name, len);
    stl_shadow_t *s = stl_shadow_create(c);
    if (s && name && len) {
        size_t n = len < 255 ? len : 255;
        memcpy(s->host, name, n);
        s->host[n] = '\0';
        /* rebuild mbed after late domain discovery to preserve sni */
        if (s->state != -1)
            stl_shadow_reset_mbed(s);
    }
    return r;
}

static OSStatus (*orig_SSLHandshake)(SSLContextRef);
static OSStatus stl_SSLHandshake(SSLContextRef c) {
    if (!stl_gate_is_active()) return orig_SSLHandshake(c);
    stl_ensure_ready();
    stl_shadow_t *s = stl_shadow_get(c);
    if (!s || !s->rf || !s->wf || !s->conn || s->client_cert)
        return orig_SSLHandshake(c);
    /* bypass only for this ctx after permanent mbed_init fail */
    if (s->state == -1) return orig_SSLHandshake(c);
    if (g_sys_fallback && stl_host_is_failed(s->host)) {
        s->state = -1;
        return orig_SSLHandshake(c);
    }
    if (s->state == 2) return noErr; /* already done */
    if (!s->inited) {
        int mi = stl_mbed_init(s);
        if (mi) {
            /* only permanent init fail uses system tls + short host blacklist */
            s->state = -1;
            stl_log("mbed_init failed (-0x%x) host=%s -> system tls", -mi, s->host);
            if (g_sys_fallback) stl_host_mark_failed(s->host);
            return orig_SSLHandshake(c);
        }
        s->state = 1;
    }
    int ret = mbedtls_ssl_handshake(&s->ssl);
    if (ret == MBEDTLS_ERR_SSL_WANT_READ || ret == MBEDTLS_ERR_SSL_WANT_WRITE)
        return errSSLWouldBlock;
    if (ret == 0) {
        s->state = 2;
        stl_log("handshake ok: %s [%s] (%s)", s->host,
                mbedtls_ssl_get_version(&s->ssl),
                mbedtls_ssl_get_ciphersuite(&s->ssl));
        return noErr;
    }
    char eb[128];
    memset(eb, 0, sizeof eb);
    mbedtls_strerror(ret, eb, sizeof eb);
    stl_log("handshake fail %s: %s (-0x%x)", s->host, eb, -ret);
    /* close cleanly without handing a partial tls socket to system tls */
    stl_shadow_reset_mbed(s);
    if (ret == MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY ||
        ret == MBEDTLS_ERR_SSL_CONN_EOF)
        return ST_ClosedGraceful;
    return ST_ClosedAbort;
}

static OSStatus (*orig_SSLRead)(SSLContextRef, void *, size_t, size_t *);
static OSStatus stl_SSLRead(SSLContextRef c, void *data, size_t len, size_t *processed) {
    if (!stl_gate_is_active()) return orig_SSLRead(c, data, len, processed);
    stl_shadow_t *s = stl_shadow_get(c);
    if (!s) return orig_SSLRead(c, data, len, processed);
    if (s->state == -1) return orig_SSLRead(c, data, len, processed);
    /* avoid system reads during mbedtls handshake because they desync the wire */
    if (s->state == 1) {
        OSStatus hs = stl_SSLHandshake(c);
        if (hs != noErr) return hs;
        /* fall through to read after handshake completes */
    }
    if (s->state != 2) {
        if (processed) *processed = 0;
        return errSSLWouldBlock;
    }
    *processed = 0;
    int n, guard = 0;
    for (;;) {
        n = mbedtls_ssl_read(&s->ssl, (unsigned char *)data, len);
        if (n == MBEDTLS_ERR_SSL_RECEIVED_NEW_SESSION_TICKET) continue;
#ifdef MBEDTLS_ERR_SSL_RECEIVED_EARLY_DATA
        if (n == MBEDTLS_ERR_SSL_RECEIVED_EARLY_DATA) continue;
#endif
        if ((n == MBEDTLS_ERR_SSL_WANT_READ || n == MBEDTLS_ERR_SSL_WANT_WRITE) &&
            mbedtls_ssl_check_pending(&s->ssl) &&
            (!g_drain_guard || ++guard < DRAIN_MAX))
            continue;
        break;
    }
    if (n == MBEDTLS_ERR_SSL_WANT_READ || n == MBEDTLS_ERR_SSL_WANT_WRITE)
        return errSSLWouldBlock;
    if (n == 0 || n == MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY ||
        n == MBEDTLS_ERR_SSL_CONN_EOF)
        return ST_ClosedGraceful;
    /* tunnel drop mid-body: graceful close lets safari retry the connection
       instead of painting a permanent hard-fail interstitial*/
    if (n == MBEDTLS_ERR_NET_RECV_FAILED || n == MBEDTLS_ERR_NET_SEND_FAILED)
        return ST_ClosedGraceful;
    if (n < 0) {
        stl_log("read fail %s: -0x%x", s->host, -n);
        return ST_ClosedAbort;
    }
    *processed = (size_t)n;
    return noErr;
}

static OSStatus (*orig_SSLWrite)(SSLContextRef, const void *, size_t, size_t *);
static OSStatus stl_SSLWrite(SSLContextRef c, const void *data, size_t len, size_t *processed) {
    if (!stl_gate_is_active()) return orig_SSLWrite(c, data, len, processed);
    stl_shadow_t *s = stl_shadow_get(c);
    if (!s) return orig_SSLWrite(c, data, len, processed);
    if (s->state == -1) return orig_SSLWrite(c, data, len, processed);
    if (s->state == 1) {
        OSStatus hs = stl_SSLHandshake(c);
        if (hs != noErr) return hs;
    }
    if (s->state != 2) {
        if (processed) *processed = 0;
        return errSSLWouldBlock;
    }
    *processed = 0;
    int n = mbedtls_ssl_write(&s->ssl, (const unsigned char *)data, len);
    if (n == MBEDTLS_ERR_SSL_WANT_READ || n == MBEDTLS_ERR_SSL_WANT_WRITE)
        return errSSLWouldBlock;
    if (n == MBEDTLS_ERR_NET_RECV_FAILED || n == MBEDTLS_ERR_NET_SEND_FAILED ||
        n == MBEDTLS_ERR_SSL_CONN_EOF)
        return ST_ClosedGraceful;
    if (n < 0) {
        stl_log("write fail %s: -0x%x", s->host, -n);
        return ST_ClosedAbort;
    }
    *processed = (size_t)n;
    return noErr;
}

static OSStatus (*orig_SSLDisposeContext)(SSLContextRef);
static OSStatus stl_SSLDisposeContext(SSLContextRef c) {
    if (stl_gate_is_active()) stl_shadow_free(c);
    return orig_SSLDisposeContext(c);
}

static OSStatus (*orig_SSLClose)(SSLContextRef);
static OSStatus stl_SSLClose(SSLContextRef c) {
    if (!stl_gate_is_active()) return orig_SSLClose(c);
    stl_shadow_t *s = stl_shadow_get(c);
    if (s && s->state == 2) mbedtls_ssl_close_notify(&s->ssl);
    return orig_SSLClose(c);
}

static OSStatus (*orig_SSLGetSessionState)(SSLContextRef, SSLSessionState *);
static OSStatus stl_SSLGetSessionState(SSLContextRef c, SSLSessionState *st) {
    if (!stl_gate_is_active()) return orig_SSLGetSessionState(c, st);
    stl_shadow_t *s = stl_shadow_get(c);
    if (s && s->state == 2) {
        if (st) *st = (SSLSessionState)ST_Connected; /* connected state */
        return noErr;
    }
    if (s && s->state == 1) {
        if (st) *st = (SSLSessionState)1; /* handshaking state */
        return noErr;
    }
    return orig_SSLGetSessionState(c, st);
}

static OSStatus (*orig_SSLGetNegotiatedProtocolVersion)(SSLContextRef, SSLProtocol *);
static OSStatus stl_SSLGetNegotiatedProtocolVersion(SSLContextRef c, SSLProtocol *p) {
    if (!stl_gate_is_active()) return orig_SSLGetNegotiatedProtocolVersion(c, p);
    stl_shadow_t *s = stl_shadow_get(c);
    if (s && s->state == 2) {
        if (p) *p = (SSLProtocol)ST_TLS12;
        return noErr;
    }
    return orig_SSLGetNegotiatedProtocolVersion(c, p);
}

static OSStatus (*orig_SSLGetProtocolVersion)(SSLContextRef, SSLProtocol *);
static OSStatus stl_SSLGetProtocolVersion(SSLContextRef c, SSLProtocol *p) {
    if (!stl_gate_is_active()) return orig_SSLGetProtocolVersion(c, p);
    stl_shadow_t *s = stl_shadow_get(c);
    if (s && s->state == 2) {
        if (p) *p = (SSLProtocol)ST_TLS12;
        return noErr;
    }
    return orig_SSLGetProtocolVersion(c, p);
}

static OSStatus (*orig_SSLGetNegotiatedCipher)(SSLContextRef, SSLCipherSuite *);
static OSStatus stl_SSLGetNegotiatedCipher(SSLContextRef c, SSLCipherSuite *cipher) {
    if (!stl_gate_is_active()) return orig_SSLGetNegotiatedCipher(c, cipher);
    stl_shadow_t *s = stl_shadow_get(c);
    if (s && s->state == 2) {
        if (cipher)
            *cipher = (SSLCipherSuite)mbedtls_ssl_get_ciphersuite_id_from_ssl(&s->ssl);
        return noErr;
    }
    return orig_SSLGetNegotiatedCipher(c, cipher);
}

static OSStatus (*orig_SSLGetBufferedReadSize)(SSLContextRef, size_t *);
static OSStatus stl_SSLGetBufferedReadSize(SSLContextRef c, size_t *sz) {
    if (!stl_gate_is_active()) return orig_SSLGetBufferedReadSize(c, sz);
    stl_shadow_t *s = stl_shadow_get(c);
    if (s && s->state == 2) {
        if (sz) *sz = mbedtls_ssl_get_bytes_avail(&s->ssl);
        return noErr;
    }
    return orig_SSLGetBufferedReadSize(c, sz);
}

static OSStatus (*orig_SSLCopyPeerTrust)(SSLContextRef, SecTrustRef *);
static OSStatus stl_SSLCopyPeerTrust(SSLContextRef c, SecTrustRef *trust) {
    if (!stl_gate_is_active()) return orig_SSLCopyPeerTrust(c, trust);
    stl_shadow_t *s = stl_shadow_get(c);
    if (!s || s->state != 2 || !trust) return orig_SSLCopyPeerTrust(c, trust);
    if (stl_build_trust(s, trust)) return noErr;
    return orig_SSLCopyPeerTrust(c, trust);
}

static OSStatus (*orig_SSLGetPeerSecTrust)(SSLContextRef, SecTrustRef *);
static OSStatus stl_SSLGetPeerSecTrust(SSLContextRef c, SecTrustRef *trust) {
    if (!stl_gate_is_active()) return orig_SSLGetPeerSecTrust(c, trust);
    stl_shadow_t *s = stl_shadow_get(c);
    if (!s || s->state != 2 || !trust) return orig_SSLGetPeerSecTrust(c, trust);
    if (stl_build_trust(s, trust)) return noErr;
    return orig_SSLGetPeerSecTrust(c, trust);
}

static OSStatus (*orig_SSLCopyPeerCertificates)(SSLContextRef, CFArrayRef *);
static OSStatus stl_SSLCopyPeerCertificates(SSLContextRef c, CFArrayRef *certs) {
    if (!stl_gate_is_active()) return orig_SSLCopyPeerCertificates(c, certs);
    stl_shadow_t *s = stl_shadow_get(c);
    if (!s || s->state != 2 || !certs) return orig_SSLCopyPeerCertificates(c, certs);
    CFArrayRef arr = stl_cert_array(s);
    if (!arr) return orig_SSLCopyPeerCertificates(c, certs);
    *certs = arr;
    return noErr;
}

static OSStatus (*orig_SSLGetPeerCertificates)(SSLContextRef, CFArrayRef *);
static OSStatus stl_SSLGetPeerCertificates(SSLContextRef c, CFArrayRef *certs) {
    if (!stl_gate_is_active()) return orig_SSLGetPeerCertificates(c, certs);
    stl_shadow_t *s = stl_shadow_get(c);
    if (!s || s->state != 2 || !certs) return orig_SSLGetPeerCertificates(c, certs);
    CFArrayRef arr = stl_cert_array(s);
    if (!arr) return orig_SSLGetPeerCertificates(c, certs);
    for (CFIndex i = 0, n = CFArrayGetCount(arr); i < n; i++)
        CFRetain(CFArrayGetValueAtIndex(arr, i));
    *certs = arr;
    return noErr;
}

static OSStatus (*orig_SSLSetCertificate)(SSLContextRef, CFArrayRef);
static OSStatus stl_SSLSetCertificate(SSLContextRef c, CFArrayRef cert_refs) {
    if (!stl_gate_is_active()) return orig_SSLSetCertificate(c, cert_refs);
    stl_ensure_ready();
    stl_shadow_t *s = stl_shadow_create(c);
    if (s) s->client_cert = 1;
    return orig_SSLSetCertificate(c, cert_refs);
}

static void stl_do_ready(void) {
    stl_gate_load_tls_options(&g_allow_tls13, &g_drain_guard, &g_sys_fallback);
    mbedtls_ctr_drbg_init(&g_drbg);
    mbedtls_entropy_init(&g_entropy);
    if (mbedtls_ctr_drbg_seed(&g_drbg, mbedtls_entropy_func, &g_entropy,
                              (const unsigned char *)"senkotlsfix", 11) == 0)
        g_drbg_ready = 1;
    psa_crypto_init();
    mbedtls_x509_crt_init(&g_ca);
    g_ca_ok = (mbedtls_x509_crt_parse_file(&g_ca, stl_ca_path) == 0);
    g_trust_set = CFSetCreateMutable(kCFAllocatorDefault, 0, &kCFTypeSetCallBacks);
    g_ready = 1;
    stl_log("shadow ready drbg=%d ca=%d tls13=%d fallback=%d",
            g_drbg_ready, g_ca_ok, g_allow_tls13, g_sys_fallback);
}

static void stl_ensure_ready(void) {
    if (!stl_gate_is_active()) return;
    pthread_once(&g_once, stl_do_ready);
}

void stl_shadow_install_hooks(void) {
    struct rebinding rebs[] = {
        { "SSLSetIOFuncs", (void *)stl_SSLSetIOFuncs, (void **)&orig_SSLSetIOFuncs },
        { "SSLSetConnection", (void *)stl_SSLSetConnection, (void **)&orig_SSLSetConnection },
        { "SSLSetPeerDomainName", (void *)stl_SSLSetPeerDomainName,
          (void **)&orig_SSLSetPeerDomainName },
        { "SSLHandshake", (void *)stl_SSLHandshake, (void **)&orig_SSLHandshake },
        { "SSLRead", (void *)stl_SSLRead, (void **)&orig_SSLRead },
        { "SSLWrite", (void *)stl_SSLWrite, (void **)&orig_SSLWrite },
        { "SSLClose", (void *)stl_SSLClose, (void **)&orig_SSLClose },
        { "SSLDisposeContext", (void *)stl_SSLDisposeContext, (void **)&orig_SSLDisposeContext },
        { "SSLGetSessionState", (void *)stl_SSLGetSessionState, (void **)&orig_SSLGetSessionState },
        { "SSLGetNegotiatedProtocolVersion", (void *)stl_SSLGetNegotiatedProtocolVersion,
          (void **)&orig_SSLGetNegotiatedProtocolVersion },
        { "SSLGetBufferedReadSize", (void *)stl_SSLGetBufferedReadSize,
          (void **)&orig_SSLGetBufferedReadSize },
        { "SSLCopyPeerTrust", (void *)stl_SSLCopyPeerTrust, (void **)&orig_SSLCopyPeerTrust },
        { "SSLCopyPeerCertificates", (void *)stl_SSLCopyPeerCertificates,
          (void **)&orig_SSLCopyPeerCertificates },
        { "SSLSetCertificate", (void *)stl_SSLSetCertificate, (void **)&orig_SSLSetCertificate },
        { "SSLGetPeerSecTrust", (void *)stl_SSLGetPeerSecTrust, (void **)&orig_SSLGetPeerSecTrust },
        { "SSLGetPeerCertificates", (void *)stl_SSLGetPeerCertificates,
          (void **)&orig_SSLGetPeerCertificates },
        { "SSLGetProtocolVersion", (void *)stl_SSLGetProtocolVersion,
          (void **)&orig_SSLGetProtocolVersion },
        { "SSLGetNegotiatedCipher", (void *)stl_SSLGetNegotiatedCipher,
          (void **)&orig_SSLGetNegotiatedCipher },
    };
    if (rebind_symbols(rebs, sizeof(rebs) / sizeof(rebs[0])) != 0)
        stl_log("shadow rebind_symbols failed");
}
