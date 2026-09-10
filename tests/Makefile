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
CFG_SRC  = $(CORE)/config.c $(CORE)/happ.c $(CORE)/b64.c $(CORE)/profiles.c $(CJSON)
TLS_CFLAGS  := $(shell pkg-config --cflags openssl 2>/dev/null)
TLS_LDFLAGS := $(shell pkg-config --libs openssl 2>/dev/null || echo -lssl -lcrypto)

.PHONY: all test clean sanitizers fuzz fuzz-smoke

HOST_TESTS = test_ctl_server test_session_direct test_config_compat \
             test_transport_ws test_store_contract test_storefile test_routing_exec \
             test_pf_natlook test_reality_crypto test_tls13_kdf \
             test_tls13_keysched test_tls13_record test_vision \
             test_transport_pick test_awg_config test_awg_handshake test_awg_tunnel test_awg_route \
             test_xray_json test_url_headers test_grpc_core test_transport_grpc test_hpack \
             test_app_proxy test_go_config test_net_safe test_legacy_ios \
             test_subinfo test_profiles test_amnezia_bundle test_happ_links

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
	./test_url_headers
	./test_grpc_core
	./test_transport_grpc
	./test_hpack
	./test_app_proxy
	./test_go_config
	./test_net_safe
	./test_legacy_ios
	./test_subinfo
	./test_profiles
	./test_happ_links
	./test_amnezia_bundle

test_go_config: test_go_config.c $(DAEMON)/go_config.c $(CFG_SRC)
	$(CC) $(CFLAGS) $(TLS_CFLAGS) -I$(DAEMON) test_go_config.c \
	      $(DAEMON)/go_config.c $(CFG_SRC) -o $@ $(TLS_LDFLAGS)

test_url_headers: test_url_headers.c $(CORE)/url.c $(CORE)/net_safe.c $(CORE)/url.h
	$(CC) $(CFLAGS) test_url_headers.c $(CORE)/url.c $(CORE)/net_safe.c -o $@

test_net_safe: test_net_safe.c $(NET_SAFE) $(CORE)/net_safe.h
	$(CC) $(CFLAGS) test_net_safe.c $(NET_SAFE) -o $@

test_subinfo: test_subinfo.c $(CORE)/subfetch.c $(CORE)/subfetch.h
	$(CC) $(CFLAGS) $(TLS_CFLAGS) -DSENKO_HOST_TEST -I$(DAEMON) test_subinfo.c \
	      $(CORE)/subfetch.c $(CORE)/http.c $(CORE)/url.c $(CORE)/net_safe.c \
	      $(CORE)/b64.c -o $@ $(TLS_LDFLAGS) -lz

test_legacy_ios: test_legacy_ios.c $(DAEMON)/legacy_ios.c $(DAEMON)/legacy_ios.h
	$(CC) $(CFLAGS) -I$(DAEMON) test_legacy_ios.c $(DAEMON)/legacy_ios.c -o $@

test_grpc_core: test_grpc_core.c $(CORE)/grpc_core.c $(CORE)/grpc_core.h
	$(CC) $(CFLAGS) test_grpc_core.c $(CORE)/grpc_core.c -o $@

test_awg_config: test_awg_config.c $(CORE)/awg_config.c $(CORE)/awg_config.h $(CORE)/b64.c $(CORE)/b64.h
	$(CC) $(CFLAGS) test_awg_config.c $(CORE)/awg_config.c $(CORE)/b64.c -o $@

test_amnezia_bundle: test_amnezia_bundle.c amnezia_fixture.inc $(CORE)/amnezia_bundle.c \
                     $(CORE)/amnezia_bundle.h $(CORE)/awg_config.c $(CORE)/b64.c $(CJSON)
	$(CC) $(CFLAGS) test_amnezia_bundle.c $(CORE)/amnezia_bundle.c $(CORE)/awg_config.c \
	  $(CORE)/b64.c $(CJSON) -o $@ -lz

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

test_transport_grpc: test_transport_grpc.c $(CORE)/transport_pick.c $(CFG_SRC) \
                     $(CORE)/transport_tcp.c $(CORE)/transport_tls.c \
                     $(CORE)/transport_ws.c $(CORE)/transport_xhttp.c $(CORE)/hpack.c $(CORE)/reality_handshake.c \
                     $(CORE)/reality_crypto.c $(CORE)/reality_auth.c \
                     $(CORE)/tls_clienthello.c $(CORE)/tls13_kdf.c \
                     $(CORE)/tls13_keysched.c $(CORE)/tls13_record.c \
                     $(CORE)/tls13_transcript.c $(CORE)/tls13_handshake.c $(TRACE) $(REPLAY) $(UPLOAD) $(CORE)/grpc_core.c
	$(CC) $(CFLAGS) $(TLS_CFLAGS) test_transport_grpc.c $(CORE)/transport_pick.c \
	      $(CFG_SRC) \
	      $(CORE)/transport_tcp.c $(CORE)/transport_tls.c $(CORE)/transport_ws.c \
	      $(CORE)/transport_xhttp.c $(CORE)/hpack.c \
	      $(CORE)/reality_handshake.c $(CORE)/reality_crypto.c $(CORE)/reality_auth.c \
	      $(CORE)/tls_clienthello.c $(CORE)/tls13_kdf.c $(CORE)/tls13_keysched.c \
	      $(CORE)/tls13_record.c $(CORE)/tls13_transcript.c $(CORE)/tls13_handshake.c \
	      $(TRACE) $(REPLAY) $(UPLOAD) $(CORE)/grpc_core.c $(TLS_LDFLAGS) -o $@

test_transport_pick: test_transport_pick.c $(CORE)/transport_pick.c $(CFG_SRC) \
                     $(CORE)/transport_tcp.c $(CORE)/transport_tls.c \
                     $(CORE)/transport_ws.c $(CORE)/transport_xhttp.c $(CORE)/hpack.c $(CORE)/reality_handshake.c \
                     $(CORE)/reality_crypto.c $(CORE)/reality_auth.c \
                     $(CORE)/tls_clienthello.c $(CORE)/tls13_kdf.c \
                     $(CORE)/tls13_keysched.c $(CORE)/tls13_record.c \
                      $(CORE)/tls13_transcript.c $(CORE)/tls13_handshake.c $(TRACE) $(REPLAY) $(UPLOAD) $(CORE)/grpc_core.c
	$(CC) $(CFLAGS) $(TLS_CFLAGS) test_transport_pick.c $(CORE)/transport_pick.c \
	      $(CFG_SRC) \
	      $(CORE)/transport_tcp.c $(CORE)/transport_tls.c $(CORE)/transport_ws.c \
	      $(CORE)/transport_xhttp.c $(CORE)/hpack.c \
	      $(CORE)/reality_handshake.c $(CORE)/reality_crypto.c $(CORE)/reality_auth.c \
	      $(CORE)/tls_clienthello.c $(CORE)/tls13_kdf.c $(CORE)/tls13_keysched.c \
	      $(CORE)/tls13_record.c $(CORE)/tls13_transcript.c $(CORE)/tls13_handshake.c \
      $(TRACE) $(REPLAY) $(UPLOAD) $(CORE)/grpc_core.c -o $@ $(TLS_LDFLAGS)

test_routing_exec: test_routing_exec.c $(DAEMON)/routing_exec.c $(DAEMON)/routing.c \
                   $(DAEMON)/legacy_ios.c $(DAEMON)/routing_exec.h $(DAEMON)/routing.h
	$(CC) $(CFLAGS) -I$(DAEMON) test_routing_exec.c $(DAEMON)/routing_exec.c \
	      $(DAEMON)/routing.c $(DAEMON)/legacy_ios.c $(NET_SAFE) -o $@

test_pf_natlook: test_pf_natlook.c $(DAEMON)/pf_natlook.c $(DAEMON)/pf_natlook.h
	$(CC) $(CFLAGS) -D_POSIX_C_SOURCE=200809L -DSENKO_HOST_TEST -I$(DAEMON) test_pf_natlook.c \
	      $(DAEMON)/pf_natlook.c -o $@

test_app_proxy: test_app_proxy.c ../senkotlsfix/stl_proxy.c ../senkotlsfix/stl_proxy.h
	$(CC) $(CFLAGS) $(THREAD_CFLAGS) -DSENKO_HOST_TEST \
	      -DC_PROXY_STATE='"/tmp/senko-c-proxy-test"' \
	      -I../senkotlsfix test_app_proxy.c ../senkotlsfix/stl_proxy.c \
	      -o $@ $(THREAD_LDFLAGS)

test_ctl_server: test_ctl_server.c $(DAEMON)/ctl_server.c $(CORE)/ctl_engine.c \
                 $(CORE)/control.c $(CORE)/store.c $(CORE)/url.c $(NET_SAFE) $(CFG_SRC)
	$(CC) $(CFLAGS) $(TLS_CFLAGS) -I$(DAEMON) test_ctl_server.c $(DAEMON)/ctl_server.c \
	      $(CORE)/ctl_engine.c $(CORE)/control.c $(CORE)/store.c $(CORE)/url.c \
	      $(NET_SAFE) $(CFG_SRC) -o $@ $(TLS_LDFLAGS)

test_profiles: test_profiles.c $(CFG_SRC)
	$(CC) $(CFLAGS) $(TLS_CFLAGS) test_profiles.c $(CFG_SRC) -o $@ $(TLS_LDFLAGS)

test_happ_links: test_happ_links.c $(CFG_SRC)
	$(CC) $(CFLAGS) $(TLS_CFLAGS) test_happ_links.c $(CFG_SRC) -o $@ $(TLS_LDFLAGS)

clean:
	rm -f $(HOST_TESTS) sub_audit fuzz_uri fuzz_json fuzz_http fuzz_h2_grpc fuzz_wire fuzz_ws fuzz_ws

sanitizers:
	$(MAKE) clean
	ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 \
	$(MAKE) test CC=/usr/bin/clang CFLAGS='-std=c99 -Wall -Wextra -O1 -g -fno-omit-frame-pointer -fsanitize=address,undefined -I../daemon/core -I../daemon/core/third_party' \
	TLS_LDFLAGS='-lssl -lcrypto -fsanitize=address,undefined'

FUZZ_FLAGS = -std=c99 -O1 -g -fno-omit-frame-pointer -fsanitize=fuzzer,address,undefined \
             -I../daemon/core -I../daemon/core/third_party

fuzz: fuzz_uri fuzz_json fuzz_http fuzz_h2_grpc fuzz_wire fuzz_ws fuzz_vision

fuzz_uri: fuzz_uri.c $(CFG_SRC)
	/usr/bin/clang $(FUZZ_FLAGS) fuzz_uri.c $(CFG_SRC) -o $@ -lssl -lcrypto

fuzz_json: fuzz_json.c $(CFG_SRC)
	/usr/bin/clang $(FUZZ_FLAGS) fuzz_json.c $(CFG_SRC) -o $@ -lssl -lcrypto

fuzz_http: fuzz_http.c $(CORE)/http.c
	/usr/bin/clang $(FUZZ_FLAGS) fuzz_http.c $(CORE)/http.c -o $@

fuzz_h2_grpc: fuzz_h2_grpc.c $(CORE)/h2_core.c $(CORE)/grpc_core.c $(CORE)/hpack.c
	/usr/bin/clang $(FUZZ_FLAGS) fuzz_h2_grpc.c $(CORE)/h2_core.c $(CORE)/grpc_core.c $(CORE)/hpack.c -o $@

fuzz_wire: fuzz_wire.c $(CORE)/socks5.c $(CORE)/vless.c $(CORE)/url.c $(NET_SAFE)
	/usr/bin/clang $(FUZZ_FLAGS) fuzz_wire.c $(CORE)/socks5.c $(CORE)/vless.c \
	      $(CORE)/url.c $(NET_SAFE) -o $@

fuzz_ws: fuzz_ws.c $(CORE)/transport_ws.c $(CORE)/transport_tcp.c $(CORE)/transport_tls.c
	/usr/bin/clang $(FUZZ_FLAGS) fuzz_ws.c $(CORE)/transport_ws.c \
	      $(CORE)/transport_tcp.c $(CORE)/transport_tls.c -o $@ -lssl -lcrypto

fuzz_vision: fuzz_vision.c $(CORE)/vision.c
	/usr/bin/clang $(FUZZ_FLAGS) fuzz_vision.c $(CORE)/vision.c -o $@

fuzz-smoke: fuzz
	./fuzz_uri -runs=1000
	./fuzz_json -runs=1000
	./fuzz_http -runs=1000
	./fuzz_h2_grpc -runs=1000
	./fuzz_wire -runs=1000
	./fuzz_ws -runs=1000
	./fuzz_vision -runs=1000

test_hpack: test_hpack.c $(CORE)/hpack.c
	$(CC) $(CFLAGS) test_hpack.c $(CORE)/hpack.c -o $@
