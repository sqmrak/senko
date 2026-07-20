# build the host daemon to validate linkage and local proxy behavior
# device builds use the ios makefile and static target libraries
CC      ?= cc
CFLAGS  ?= -std=c99 -Wall -Wextra -O2 -Icore -Icore/third_party -I.
THREAD_CFLAGS  ?= -pthread
THREAD_LDFLAGS ?= -pthread
# resolve host tls libraries through pkg-config
TLS_CFLAGS  := $(shell pkg-config --cflags openssl 2>/dev/null)
TLS_LDFLAGS := $(shell pkg-config --libs openssl 2>/dev/null || echo -lssl -lcrypto)
LDFLAGS ?=

CORE_SRC = core/vless.c core/b64.c core/config.c core/happ.c core/third_party/cJSON.c core/net_safe.c core/socks5.c core/senko_trace.c core/senko_replay.c core/senko_upload.c \
           core/vless_conn.c core/session.c core/store.c core/control.c \
           core/ctl_engine.c core/http.c core/url.c core/subfetch.c \
           core/transport_tcp.c core/transport_tls.c core/transport_ws.c \
           core/transport_xhttp.c core/transport_pick.c \
           core/reality_crypto.c core/reality_auth.c core/tls_clienthello.c \
           core/tls13_kdf.c core/tls13_keysched.c core/tls13_record.c \
           core/tls13_transcript.c core/tls13_handshake.c core/reality_handshake.c \
           core/socks5_client.c core/http_client.c core/vision.c core/awg_config.c core/awg_handshake.c
DAEMON_SRC = dialer.c loop.c pf_natlook.c ctl_server.c daemon_ctl.c storefile.c settings.c vpn_icon.c \
             routing.c routing_exec.c main.c

.PHONY: all clean

all: senkod senkoctl senkoawgd

senkod: $(CORE_SRC) $(DAEMON_SRC)
	$(CC) $(CFLAGS) $(THREAD_CFLAGS) $(TLS_CFLAGS) $(CORE_SRC) $(DAEMON_SRC) -o $@ $(LDFLAGS) $(TLS_LDFLAGS) -lz $(THREAD_LDFLAGS)

# senkoctl decodes the plain control protocol and fetch payloads
senkoctl: senkoctl.c core/b64.c
	$(CC) $(CFLAGS) senkoctl.c core/b64.c -o $@ $(LDFLAGS)

senkoawgd: senkoawgd.c awg_utun.c awg_route.c awg_pfroute.c core/awg_config.c core/awg_handshake.c core/awg_tunnel.c core/b64.c core/reality_crypto.c vpn_icon.c
	$(CC) $(CFLAGS) $(TLS_CFLAGS) senkoawgd.c awg_utun.c awg_route.c awg_pfroute.c core/awg_config.c core/awg_handshake.c \
	      core/awg_tunnel.c core/b64.c core/reality_crypto.c vpn_icon.c -o $@ $(LDFLAGS) $(TLS_LDFLAGS)

clean:
	rm -f senkod senkoctl senkoawgd
