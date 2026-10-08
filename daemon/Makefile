# build the host daemon to validate linkage and local proxy behavior
# device builds use the ios makefile and static target libraries
CC      ?= cc
CFLAGS  ?= -std=c99 -Wall -Wextra -O2 -Icore -Icore/third_party -I.
THREAD_CFLAGS  ?= -pthread
THREAD_LDFLAGS ?= -pthread
# resolve host tls libraries through pkg-config
TLS_CFLAGS  := $(shell pkg-config --cflags openssl 2>/dev/null)
TLS_LDFLAGS := $(shell pkg-config --libs openssl 2>/dev/null || echo -lssl -lcrypto)
# the hysteria2 client needs ngtcp2 with its openssl backend, and nghttp3
QUIC_PKGS   = libngtcp2 libngtcp2_crypto_ossl libnghttp3
QUIC_CFLAGS := $(shell pkg-config --cflags $(QUIC_PKGS) 2>/dev/null)
QUIC_LDFLAGS := $(shell pkg-config --libs $(QUIC_PKGS) 2>/dev/null || echo -lngtcp2 -lngtcp2_crypto_ossl -lnghttp3)
LDFLAGS ?=

CORE_SRC = core/vless.c core/b64.c core/config.c core/share_link.c core/rules.c core/dns_msg.c core/dns_cache.c core/profiles.c core/happ.c core/happ_crypt5.c core/third_party/cJSON.c core/net_safe.c core/socks5.c core/senko_trace.c core/senko_replay.c core/senko_upload.c core/senko_time.c core/real_probe.c core/grpc_core.c core/h2_core.c \
           core/vless_conn.c core/session.c core/store.c core/control.c \
           core/ctl_engine.c core/http.c core/url.c core/subfetch.c \
           core/transport_tcp.c core/transport_tls.c core/transport_ws.c \
           core/transport_xhttp.c core/transport_pick.c core/hpack.c \
           core/reality_crypto.c core/reality_auth.c core/tls_clienthello.c \
           core/tls13_kdf.c core/tls13_keysched.c core/tls13_record.c \
           core/tls13_transcript.c core/tls13_handshake.c core/reality_handshake.c \
           core/socks5_client.c core/http_client.c core/vision.c core/awg_config.c core/awg_handshake.c core/awg_tunnel.c \
           core/trojan_client.c core/shadowsocks_client.c core/blake2b256.c core/hy2_proto.c \
           core/backend_state.c core/endpoint_pool.c core/probe_stats.c core/route_message.c \
           core/tun_flow.c core/tun_nat.c core/tun_plan.c core/utun_frame.c core/vless_udp.c core/dns_policy.c core/tun_policy.c
# vendored PQClean ml-kem-768 "clean" backend, plus its shared fips202.c and a
# local randombytes.c shim over senko's own openssl rng; see core/third_party/mlkem768/LICENSE
MLKEM768_SRC = core/third_party/mlkem768/kem.c core/third_party/mlkem768/indcpa.c \
               core/third_party/mlkem768/poly.c core/third_party/mlkem768/polyvec.c \
               core/third_party/mlkem768/ntt.c core/third_party/mlkem768/cbd.c \
               core/third_party/mlkem768/reduce.c core/third_party/mlkem768/verify.c \
               core/third_party/mlkem768/symmetric-shake.c core/third_party/mlkem768/fips202.c \
               core/third_party/mlkem768/randombytes.c
CORE_SRC += $(MLKEM768_SRC)
DAEMON_SRC = dialer.c loop.c ctl_server.c daemon_ctl.c storefile.c settings.c status.c \
             app_proxy.c egress.c senko_core_config.c senko_core_backend.c \
             awg_utun.c awg_route.c awg_pfroute.c legacy_ios.c proc_detach.c \
             route_socket.c tun_dialer.c tun_loop.c tun_opener.c tun_stack.c tun_tcp.c tun_udp.c \
             hy2_engine.c utun_device.c utun_route.c utun_dns.c utun_backend.c direct_socket.c direct_dns.c helper_jobs.c senkod_helper.c awg_link.c awg_backend.c main.c
LWIP_DIR = third_party/lwip/src
LWIP_SRC = $(wildcard $(LWIP_DIR)/core/*.c) $(wildcard $(LWIP_DIR)/core/ipv4/*.c) \
           $(wildcard $(LWIP_DIR)/core/ipv6/*.c) lwip_port/lwip_sys.c
TUN_CFLAGS = -Ilwip_port -I$(LWIP_DIR)/include

.PHONY: all clean

all: senkod senkoctl

senkod: $(CORE_SRC) $(DAEMON_SRC) $(LWIP_SRC)
	$(CC) $(CFLAGS) $(TUN_CFLAGS) $(THREAD_CFLAGS) $(TLS_CFLAGS) $(QUIC_CFLAGS) $(CORE_SRC) $(DAEMON_SRC) $(LWIP_SRC) -o $@ $(LDFLAGS) $(QUIC_LDFLAGS) $(TLS_LDFLAGS) -lz $(THREAD_LDFLAGS)

# senkoctl decodes the plain control protocol and fetch payloads
senkoctl: senkoctl.c core/b64.c
	$(CC) $(CFLAGS) senkoctl.c core/b64.c -o $@ $(LDFLAGS)

clean:
	rm -f senkod senkoctl
