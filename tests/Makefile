# build platform-free host tests with plain clang
# keep the aggregate list explicit so missing tests fail at build time
CC      ?= cc
CFLAGS  ?= -std=c99 -Wall -Wextra -O2 -I../daemon/core -I../daemon/core/third_party
THREAD_CFLAGS  ?= -pthread
THREAD_LDFLAGS ?= -pthread
CORE     = ../daemon/core
DAEMON   = ../daemon
TRACE    = $(CORE)/senko_trace.c
REPLAY   = $(CORE)/senko_replay.c
UPLOAD   = $(CORE)/senko_upload.c
NET_SAFE = $(CORE)/net_safe.c
CJSON    = $(CORE)/third_party/cJSON.c
CFG_SRC  = $(CORE)/config.c $(CORE)/happ.c $(CORE)/b64.c $(CJSON)
TLS_CFLAGS  := $(shell pkg-config --cflags openssl 2>/dev/null)
TLS_LDFLAGS := $(shell pkg-config --libs openssl 2>/dev/null || echo -lssl -lcrypto)

.PHONY: all test clean

HOST_TESTS = test_ctl_server test_session_direct test_config_compat \
             test_transport_ws test_store_contract test_storefile test_routing_exec \
             test_pf_natlook test_reality_crypto test_tls13_kdf \
             test_tls13_keysched test_tls13_record test_vision \
             test_transport_pick test_awg_config test_awg_handshake test_awg_tunnel test_awg_route \
             test_xray_json

all: test

test: $(HOST_TESTS)
	./test_ctl_server
	./test_session_direct
	./test_config_compat
	./test_transport_ws
	./test_store_contract
	./test_storefile
	./test_routing_exec
	./test_pf_natlook
	./test_reality_crypto
	./test_tls13_kdf
	./test_tls13_keysched
	./test_tls13_record
	./test_vision
	./test_transport_pick
	./test_awg_config
	./test_awg_handshake
	./test_awg_tunnel
	./test_awg_route
	./test_xray_json

test_awg_config: test_awg_config.c $(CORE)/awg_config.c $(CORE)/awg_config.h $(CORE)/b64.c $(CORE)/b64.h
	$(CC) $(CFLAGS) test_awg_config.c $(CORE)/awg_config.c $(CORE)/b64.c -o $@

test_awg_handshake: test_awg_handshake.c $(CORE)/awg_handshake.c $(CORE)/awg_handshake.h \
                    $(CORE)/awg_config.c $(CORE)/b64.c $(CORE)/reality_crypto.c
	$(CC) $(CFLAGS) $(TLS_CFLAGS) test_awg_handshake.c $(CORE)/awg_handshake.c \
	      $(CORE)/awg_config.c $(CORE)/b64.c $(CORE)/reality_crypto.c -o $@ $(TLS_LDFLAGS)

test_awg_tunnel: test_awg_tunnel.c $(CORE)/awg_tunnel.c $(CORE)/awg_tunnel.h \
                 $(CORE)/awg_handshake.c $(CORE)/awg_config.c $(CORE)/b64.c $(CORE)/reality_crypto.c
	$(CC) $(CFLAGS) $(TLS_CFLAGS) test_awg_tunnel.c $(CORE)/awg_tunnel.c \
	      $(CORE)/awg_handshake.c $(CORE)/awg_config.c $(CORE)/b64.c $(CORE)/reality_crypto.c \
	      -o $@ $(TLS_LDFLAGS)

test_awg_route: test_awg_route.c $(DAEMON)/awg_route.c $(DAEMON)/awg_route.h $(CORE)/awg_config.c $(CORE)/b64.c
	$(CC) $(CFLAGS) -I$(DAEMON) test_awg_route.c $(DAEMON)/awg_route.c $(CORE)/awg_config.c $(CORE)/b64.c -o $@

test_config_compat: test_config_compat.c $(CFG_SRC) $(CORE)/config.h $(CORE)/b64.h $(CORE)/happ.h
	$(CC) $(CFLAGS) $(TLS_CFLAGS) test_config_compat.c $(CFG_SRC) -o $@ $(TLS_LDFLAGS)

test_xray_json: test_xray_json.c $(CFG_SRC) $(CORE)/config.h
	$(CC) $(CFLAGS) $(TLS_CFLAGS) test_xray_json.c $(CFG_SRC) -o $@ $(TLS_LDFLAGS)

sub_audit: sub_audit.c $(CFG_SRC) $(CORE)/config.h $(CORE)/b64.h
	$(CC) $(CFLAGS) $(TLS_CFLAGS) sub_audit.c $(CFG_SRC) -o $@ $(TLS_LDFLAGS)

test_transport_ws: test_transport_ws.c $(CORE)/transport_ws.c $(CORE)/transport_tcp.c $(CORE)/transport_tls.c $(CORE)/transport.h
	$(CC) $(CFLAGS) $(TLS_CFLAGS) test_transport_ws.c $(CORE)/transport_ws.c \
	      $(CORE)/transport_tcp.c $(CORE)/transport_tls.c -o $@ $(TLS_LDFLAGS)

test_store_contract: test_store_contract.c $(CORE)/store.c $(CFG_SRC) $(CORE)/store.h
	$(CC) $(CFLAGS) $(TLS_CFLAGS) test_store_contract.c $(CORE)/store.c $(CFG_SRC) -o $@ $(TLS_LDFLAGS)

test_storefile: test_storefile.c $(DAEMON)/storefile.c $(DAEMON)/settings.c \
                $(CORE)/store.c $(CFG_SRC)
	$(CC) $(CFLAGS) $(TLS_CFLAGS) -I$(DAEMON) test_storefile.c $(DAEMON)/storefile.c \
	      $(DAEMON)/settings.c $(CORE)/store.c $(CFG_SRC) -o $@ $(TLS_LDFLAGS)

test_session_direct: test_session_direct.c $(CORE)/session.c $(CORE)/socks5.c $(NET_SAFE) \
              $(CORE)/vless.c $(CORE)/vless_conn.c $(CORE)/socks5_client.c $(CORE)/http_client.c $(CORE)/vision.c $(TRACE) $(REPLAY) $(UPLOAD)
	$(CC) $(CFLAGS) test_session_direct.c $(CORE)/session.c \
	      $(CORE)/socks5.c $(NET_SAFE) $(CORE)/vless.c $(CORE)/vless_conn.c \
	      $(CORE)/socks5_client.c $(CORE)/http_client.c $(CORE)/vision.c $(TRACE) $(REPLAY) $(UPLOAD) -o $@

test_reality_crypto: test_reality_crypto.c $(CORE)/reality_crypto.c $(CORE)/reality_crypto.h
	$(CC) $(CFLAGS) $(TLS_CFLAGS) test_reality_crypto.c $(CORE)/reality_crypto.c \
	      -o $@ $(TLS_LDFLAGS)

test_tls13_kdf: test_tls13_kdf.c $(CORE)/tls13_kdf.c $(CORE)/reality_crypto.c
	$(CC) $(CFLAGS) $(TLS_CFLAGS) test_tls13_kdf.c $(CORE)/tls13_kdf.c \
	      $(CORE)/reality_crypto.c -o $@ $(TLS_LDFLAGS)

test_tls13_keysched: test_tls13_keysched.c $(CORE)/tls13_keysched.c $(CORE)/tls13_kdf.c \
                     $(CORE)/reality_crypto.c
	$(CC) $(CFLAGS) $(TLS_CFLAGS) test_tls13_keysched.c $(CORE)/tls13_keysched.c \
	      $(CORE)/tls13_kdf.c $(CORE)/reality_crypto.c -o $@ $(TLS_LDFLAGS)

test_tls13_record: test_tls13_record.c $(CORE)/tls13_record.c $(CORE)/reality_crypto.c
	$(CC) $(CFLAGS) $(TLS_CFLAGS) test_tls13_record.c $(CORE)/tls13_record.c \
	      $(CORE)/reality_crypto.c -o $@ $(TLS_LDFLAGS)

test_vision: test_vision.c $(CORE)/vision.c $(CORE)/vision.h
	$(CC) $(CFLAGS) test_vision.c $(CORE)/vision.c -o $@

test_transport_pick: test_transport_pick.c $(CORE)/transport_pick.c $(CFG_SRC) \
                     $(CORE)/transport_tcp.c $(CORE)/transport_tls.c \
                     $(CORE)/transport_ws.c $(CORE)/transport_xhttp.c $(CORE)/reality_handshake.c \
                     $(CORE)/reality_crypto.c $(CORE)/reality_auth.c \
                     $(CORE)/tls_clienthello.c $(CORE)/tls13_kdf.c \
                     $(CORE)/tls13_keysched.c $(CORE)/tls13_record.c \
                     $(CORE)/tls13_transcript.c $(CORE)/tls13_handshake.c $(TRACE) $(REPLAY) $(UPLOAD)
	$(CC) $(CFLAGS) $(TLS_CFLAGS) test_transport_pick.c $(CORE)/transport_pick.c \
	      $(CFG_SRC) \
	      $(CORE)/transport_tcp.c $(CORE)/transport_tls.c $(CORE)/transport_ws.c \
	      $(CORE)/transport_xhttp.c \
	      $(CORE)/reality_handshake.c $(CORE)/reality_crypto.c $(CORE)/reality_auth.c \
	      $(CORE)/tls_clienthello.c $(CORE)/tls13_kdf.c $(CORE)/tls13_keysched.c \
	      $(CORE)/tls13_record.c $(CORE)/tls13_transcript.c $(CORE)/tls13_handshake.c \
	      $(TRACE) $(REPLAY) $(UPLOAD) -o $@ $(TLS_LDFLAGS)

test_routing_exec: test_routing_exec.c $(DAEMON)/routing_exec.c $(DAEMON)/routing.c \
                   $(DAEMON)/routing_exec.h $(DAEMON)/routing.h
	$(CC) $(CFLAGS) -I$(DAEMON) test_routing_exec.c $(DAEMON)/routing_exec.c \
	      $(DAEMON)/routing.c $(NET_SAFE) -o $@

test_pf_natlook: test_pf_natlook.c $(DAEMON)/pf_natlook.c $(DAEMON)/pf_natlook.h
	$(CC) $(CFLAGS) -D_POSIX_C_SOURCE=200809L -DSENKO_HOST_TEST -I$(DAEMON) test_pf_natlook.c \
	      $(DAEMON)/pf_natlook.c -o $@

test_ctl_server: test_ctl_server.c $(DAEMON)/ctl_server.c $(CORE)/ctl_engine.c \
                 $(CORE)/control.c $(CORE)/store.c $(CFG_SRC)
	$(CC) $(CFLAGS) $(TLS_CFLAGS) -I$(DAEMON) test_ctl_server.c $(DAEMON)/ctl_server.c \
	      $(CORE)/ctl_engine.c $(CORE)/control.c $(CORE)/store.c \
	      $(CFG_SRC) -o $@ $(TLS_LDFLAGS)

clean:
	rm -f $(HOST_TESTS) sub_audit
