# build platform-free host tests with plain clang
# keep the aggregate list explicit so missing tests fail at build time
CC      ?= cc
CFLAGS  ?= -std=c99 -Wall -Wextra -O2 -I../daemon/core -I../daemon/core/third_party
ZBAR_CFLAGS = -DSENKO_ZBAR_IOS -w -I../app/third_party/zbar/include \
              -I../app/third_party/zbar/zbar -I../app/third_party/zbar/zbar/decoder \
              -I../app/third_party/zbar/zbar/qrcode
ZBAR_SRC = ../app/third_party/zbar/zbar/config.c \
           ../app/third_party/zbar/zbar/error.c \
           ../app/third_party/zbar/zbar/symbol.c \
           ../app/third_party/zbar/zbar/image.c \
           ../app/third_party/zbar/zbar/refcnt.c \
           ../app/third_party/zbar/zbar/img_scanner.c \
           ../app/third_party/zbar/zbar/scanner.c \
           ../app/third_party/zbar/zbar/decoder.c \
           ../app/third_party/zbar/zbar/misc.c \
           ../app/third_party/zbar/zbar/decoder/qr_finder.c \
           ../app/third_party/zbar/zbar/qrcode/qrdec.c \
           ../app/third_party/zbar/zbar/qrcode/qrdectxt.c \
           ../app/third_party/zbar/zbar/qrcode/rs.c \
           ../app/third_party/zbar/zbar/qrcode/isaac.c \
           ../app/third_party/zbar/zbar/qrcode/bch15_5.c \
           ../app/third_party/zbar/zbar/qrcode/binarize.c \
           ../app/third_party/zbar/zbar/qrcode/util.c
THREAD_CFLAGS  ?= -pthread
THREAD_LDFLAGS ?= -pthread
CORE     = ../daemon/core
DAEMON   = ../daemon
TRACE    = $(CORE)/senko_trace.c
REPLAY   = $(CORE)/senko_replay.c
UPLOAD   = $(CORE)/senko_upload.c
NET_SAFE = $(CORE)/net_safe.c
CJSON    = $(CORE)/third_party/cJSON.c
CFG_SRC  = $(CORE)/config.c $(CORE)/rules.c $(CORE)/happ.c $(CORE)/happ_crypt5.c $(CORE)/b64.c $(CORE)/profiles.c $(CJSON)
TLS_CFLAGS  := $(shell pkg-config --cflags openssl 2>/dev/null)
TLS_LDFLAGS := $(shell pkg-config --libs openssl 2>/dev/null || echo -lssl -lcrypto)

.PHONY: all test clean sanitizers fuzz fuzz-smoke

HOST_TESTS = test_ctl_server test_autoconnect test_session_direct test_config_compat \
             test_transport_ws test_store_contract test_storefile test_routing_exec \
             test_pf_natlook test_reality_crypto test_tls13_kdf \
             test_tls13_keysched test_tls13_record test_tls_clienthello test_vision \
             test_transport_pick test_awg_config test_awg_handshake test_awg_tunnel test_awg_route \
             test_xray_json test_url_headers test_grpc_core test_transport_grpc test_hpack \
             test_app_proxy test_go_config test_net_safe test_legacy_ios \
             test_subinfo test_profiles test_amnezia_bundle test_happ_links \
             test_b64 test_traffic test_loop_traffic test_rules
HOST_TESTS += test_dns_msg
HOST_TESTS += test_dns_cache
HOST_TESTS += test_pf_table
HOST_TESTS += test_trojan
HOST_TESTS += test_shadowsocks
HOST_TESTS += test_qr_zbar

all: test

test: $(HOST_TESTS)
	./test_trojan
	./test_shadowsocks
	./test_qr_zbar
	./test_traffic
	./test_loop_traffic
	./test_rules
	./test_dns_msg
	./test_dns_cache
	./test_pf_table
	./test_ctl_server
	./test_autoconnect
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
	./test_tls_clienthello
	./test_vision
	./test_transport_pick
	./test_awg_config
	./test_awg_handshake
	./test_awg_tunnel
	./test_awg_route
	./test_xray_json
	./test_b64
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

test_traffic: test_traffic.c $(CORE)/control.c $(CORE)/traffic.h
	$(CC) $(CFLAGS) test_traffic.c $(CORE)/control.c -o $@

test_rules: test_rules.c $(CORE)/rules.c $(CORE)/rules.h
	$(CC) $(CFLAGS) test_rules.c $(CORE)/rules.c -o $@

test_dns_msg: test_dns_msg.c $(CORE)/dns_msg.c $(CORE)/dns_msg.h
	$(CC) $(CFLAGS) test_dns_msg.c $(CORE)/dns_msg.c -o $@

test_dns_cache: test_dns_cache.c $(CORE)/dns_cache.c $(CORE)/dns_cache.h \
                $(CORE)/dns_msg.c $(CORE)/dns_msg.h $(CORE)/rules.h
	$(CC) $(CFLAGS) test_dns_cache.c $(CORE)/dns_cache.c $(CORE)/dns_msg.c -o $@

test_pf_table: test_pf_table.c $(DAEMON)/pf_table.c $(DAEMON)/pf_table.h $(CORE)/rules.h
	$(CC) $(CFLAGS) -I$(DAEMON) test_pf_table.c $(DAEMON)/pf_table.c -o $@

test_loop_traffic: test_loop_traffic.c $(DAEMON)/loop.c $(CORE)/session.c
	$(CC) $(CFLAGS) $(TLS_CFLAGS) $(THREAD_CFLAGS) -DSENKO_RELEASE -I$(DAEMON) test_loop_traffic.c \
	      $(DAEMON)/pf_natlook.c $(CORE)/session.c $(CORE)/socks5.c $(NET_SAFE) \
	      $(CORE)/vless.c $(CORE)/vless_conn.c $(CORE)/socks5_client.c \
	      $(CORE)/http_client.c $(CORE)/vision.c $(CORE)/transport_tcp.c \
	      $(CORE)/trojan_client.c $(CORE)/shadowsocks_client.c $(CORE)/reality_crypto.c \
	      $(TRACE) $(REPLAY) $(UPLOAD) -o $@ $(TLS_LDFLAGS) $(THREAD_LDFLAGS)

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

test_b64: test_b64.c $(CORE)/b64.c $(CORE)/b64.h
	$(CC) $(CFLAGS) test_b64.c $(CORE)/b64.c -o $@

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
              $(CORE)/vless.c $(CORE)/vless_conn.c $(CORE)/socks5_client.c $(CORE)/http_client.c $(CORE)/vision.c \
              $(CORE)/trojan_client.c $(CORE)/shadowsocks_client.c $(CORE)/reality_crypto.c $(TRACE) $(REPLAY) $(UPLOAD)
	$(CC) $(CFLAGS) $(TLS_CFLAGS) test_session_direct.c $(CORE)/session.c \
	      $(CORE)/socks5.c $(NET_SAFE) $(CORE)/vless.c $(CORE)/vless_conn.c \
	      $(CORE)/socks5_client.c $(CORE)/http_client.c $(CORE)/vision.c \
	      $(CORE)/trojan_client.c $(CORE)/shadowsocks_client.c $(CORE)/reality_crypto.c \
	      $(TRACE) $(REPLAY) $(UPLOAD) -o $@ $(TLS_LDFLAGS)

test_trojan: test_trojan.c $(CORE)/trojan_client.c $(CFG_SRC)
	$(CC) $(CFLAGS) $(TLS_CFLAGS) test_trojan.c $(CORE)/trojan_client.c $(CFG_SRC) -o $@ $(TLS_LDFLAGS)

test_shadowsocks: test_shadowsocks.c $(CORE)/shadowsocks_client.c $(CORE)/reality_crypto.c $(CFG_SRC)
	$(CC) $(CFLAGS) $(TLS_CFLAGS) test_shadowsocks.c $(CORE)/shadowsocks_client.c $(CORE)/reality_crypto.c $(CFG_SRC) -o $@ $(TLS_LDFLAGS)

test_qr_zbar: test_qr_zbar.c $(ZBAR_SRC)
	$(CC) $(ZBAR_CFLAGS) $(CFLAGS) test_qr_zbar.c $(ZBAR_SRC) -o $@

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

test_tls_clienthello: test_tls_clienthello.c $(CORE)/tls_clienthello.c $(CORE)/tls_clienthello.h
	$(CC) $(CFLAGS) test_tls_clienthello.c $(CORE)/tls_clienthello.c -o $@

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
                   $(DAEMON)/legacy_ios.c $(DAEMON)/routing_exec.h $(DAEMON)/routing.h \
                   $(CORE)/rules.c $(CORE)/rules.h $(CORE)/dns_msg.c \
                   $(CORE)/dns_cache.c $(DAEMON)/pf_table.c
	$(CC) $(CFLAGS) -I$(DAEMON) test_routing_exec.c $(DAEMON)/routing_exec.c \
	      $(DAEMON)/routing.c $(DAEMON)/legacy_ios.c $(NET_SAFE) $(CORE)/rules.c \
	      $(CORE)/dns_msg.c $(CORE)/dns_cache.c $(DAEMON)/pf_table.c -o $@

test_pf_natlook: test_pf_natlook.c $(DAEMON)/pf_natlook.c $(DAEMON)/pf_natlook.h
	$(CC) $(CFLAGS) -D_POSIX_C_SOURCE=200809L -DSENKO_HOST_TEST -I$(DAEMON) test_pf_natlook.c \
	      $(DAEMON)/pf_natlook.c -o $@

test_app_proxy: test_app_proxy.c ../senkotlsfix/stl_proxy.c ../senkotlsfix/stl_proxy.h
	$(CC) $(CFLAGS) $(THREAD_CFLAGS) -DSENKO_HOST_TEST \
	      -DC_PROXY_STATE='"/tmp/senko-c-proxy-test"' \
	      -I../senkotlsfix test_app_proxy.c ../senkotlsfix/stl_proxy.c \
	      -o $@ $(THREAD_LDFLAGS)

test_ctl_server: test_ctl_server.c $(DAEMON)/ctl_server.c $(CORE)/ctl_engine.c \
                 $(CORE)/control.c $(CORE)/store.c $(CORE)/url.c $(NET_SAFE) \
                 $(DAEMON)/settings.c $(DAEMON)/routing_exec.c $(DAEMON)/routing.c \
                 $(DAEMON)/legacy_ios.c $(CFG_SRC) $(CORE)/dns_msg.c \
                 $(CORE)/dns_cache.c $(DAEMON)/pf_table.c
	$(CC) $(CFLAGS) $(TLS_CFLAGS) -I$(DAEMON) test_ctl_server.c $(DAEMON)/ctl_server.c \
	      $(CORE)/ctl_engine.c $(CORE)/control.c $(CORE)/store.c $(CORE)/url.c \
	      $(DAEMON)/settings.c $(DAEMON)/routing_exec.c $(DAEMON)/routing.c \
	      $(DAEMON)/legacy_ios.c \
	      $(NET_SAFE) $(CFG_SRC) $(CORE)/dns_msg.c $(CORE)/dns_cache.c \
	      $(DAEMON)/pf_table.c -o $@ $(TLS_LDFLAGS)

test_autoconnect: test_autoconnect.c $(DAEMON)/ctl_server.c $(CORE)/ctl_engine.c \
                 $(CORE)/control.c $(CORE)/store.c $(CORE)/url.c $(NET_SAFE) \
                 $(DAEMON)/settings.c $(DAEMON)/routing_exec.c $(DAEMON)/routing.c \
                 $(DAEMON)/legacy_ios.c $(CFG_SRC) $(CORE)/dns_msg.c \
                 $(CORE)/dns_cache.c $(DAEMON)/pf_table.c
	$(CC) $(CFLAGS) $(TLS_CFLAGS) -I$(DAEMON) test_autoconnect.c $(DAEMON)/ctl_server.c \
	      $(CORE)/ctl_engine.c $(CORE)/control.c $(CORE)/store.c $(CORE)/url.c \
	      $(DAEMON)/settings.c $(DAEMON)/routing_exec.c $(DAEMON)/routing.c \
	      $(DAEMON)/legacy_ios.c \
	      $(NET_SAFE) $(CFG_SRC) $(CORE)/dns_msg.c $(CORE)/dns_cache.c \
	      $(DAEMON)/pf_table.c -o $@ $(TLS_LDFLAGS)

test_profiles: test_profiles.c $(CFG_SRC)
	$(CC) $(CFLAGS) $(TLS_CFLAGS) test_profiles.c $(CFG_SRC) -o $@ $(TLS_LDFLAGS)

test_happ_links: test_happ_links.c $(CFG_SRC)
	$(CC) $(CFLAGS) $(TLS_CFLAGS) test_happ_links.c $(CFG_SRC) -o $@ $(TLS_LDFLAGS)

clean:
	rm -f $(HOST_TESTS) sub_audit fuzz_uri fuzz_json fuzz_http fuzz_h2_grpc fuzz_wire fuzz_ws fuzz_vision fuzz_dns

sanitizers:
	$(MAKE) clean
	ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 \
	$(MAKE) test CC=/usr/bin/clang CFLAGS='-std=c99 -Wall -Wextra -O1 -g -fno-omit-frame-pointer -fsanitize=address,undefined -I../daemon/core -I../daemon/core/third_party' \
	TLS_LDFLAGS='-lssl -lcrypto -fsanitize=address,undefined'

FUZZ_FLAGS = -std=c99 -O1 -g -fno-omit-frame-pointer -fsanitize=fuzzer,address,undefined \
             -I../daemon/core -I../daemon/core/third_party

fuzz: fuzz_uri fuzz_json fuzz_http fuzz_h2_grpc fuzz_wire fuzz_ws fuzz_vision fuzz_dns

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

fuzz_dns: fuzz_dns.c $(CORE)/dns_msg.c $(CORE)/dns_msg.h
	/usr/bin/clang $(FUZZ_FLAGS) fuzz_dns.c $(CORE)/dns_msg.c -o $@

fuzz-smoke: fuzz
	./fuzz_uri -runs=1000
	./fuzz_json -runs=1000
	./fuzz_http -runs=1000
	./fuzz_h2_grpc -runs=1000
	./fuzz_wire -runs=1000
	./fuzz_ws -runs=1000
	./fuzz_vision -runs=1000
	./fuzz_dns -runs=1000

test_hpack: test_hpack.c $(CORE)/hpack.c
	$(CC) $(CFLAGS) test_hpack.c $(CORE)/hpack.c -o $@
