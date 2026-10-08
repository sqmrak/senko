#!/usr/bin/env bash
# runs tests/hy2_tun_probe.c: the utun tcp bridge over hysteria2 against the
# reference server, inside a private network namespace so the services can
# listen on 203.0.113.7 without touching the host's routes or addresses.
# needs unshare, ip and HY2_SERVER like hy2_probe.sh
set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
if [[ -z "${HY2_TUN_INSIDE:-}" ]]; then
  exec unshare -Urn env HY2_TUN_INSIDE=1 bash "$0" "$@"
fi
SERVER="${HY2_SERVER:?set HY2_SERVER to the hysteria server binary}"
WORK="$(mktemp -d)"
trap 'kill ${SERVER_PID:-0} 2>/dev/null || true; rm -rf "${WORK}"' EXIT
CC="${CC:-cc}"
DAEMON="${HERE}/../daemon"
CORE="${DAEMON}/core"
LWIP="${DAEMON}/third_party/lwip/src"

ip link set lo up
ip addr add 203.0.113.7/32 dev lo

"${CC}" -std=gnu99 -O2 -pthread -DSENKO_RELEASE ${HY2_CFLAGS:-} \
  -I"${HERE}" -I"${DAEMON}" -I"${CORE}" -I"${DAEMON}/lwip_port" -I"${LWIP}/include" \
  $(pkg-config --cflags libngtcp2 libngtcp2_crypto_ossl libnghttp3 openssl) \
  "${HERE}/hy2_tun_probe.c" "${DAEMON}/hy2_engine.c" "${DAEMON}/tun_tcp.c" \
  "${DAEMON}/tun_opener.c" "${DAEMON}/tun_loop.c" "${DAEMON}/utun_device.c" \
  "${DAEMON}/tun_stack.c" "${CORE}/tun_nat.c" "${CORE}/utun_frame.c" "${CORE}/dns_msg.c" \
  "${CORE}/rules.c" "${CORE}/tun_policy.c" "${CORE}/dns_policy.c" "${CORE}/dns_cache.c" \
  "${DAEMON}/direct_socket.c" "${DAEMON}/route_socket.c" "${CORE}/route_message.c" \
  "${CORE}/session.c" "${CORE}/socks5.c" "${CORE}/net_safe.c" "${CORE}/vless.c" \
  "${CORE}/vless_conn.c" "${CORE}/socks5_client.c" "${CORE}/http_client.c" "${CORE}/vision.c" \
  "${CORE}/trojan_client.c" "${CORE}/shadowsocks_client.c" "${CORE}/reality_crypto.c" \
  "${CORE}"/third_party/mlkem768/*.c "${CORE}/senko_trace.c" "${CORE}/senko_replay.c" \
  "${CORE}/senko_upload.c" "${CORE}/senko_time.c" "${CORE}/hy2_proto.c" "${CORE}/blake2b256.c" \
  "${CORE}/transport_tcp.c" "${CORE}/transport_tls.c" \
  "${LWIP}"/core/*.c "${LWIP}"/core/ipv4/*.c "${LWIP}"/core/ipv6/*.c "${DAEMON}/lwip_port/lwip_sys.c" \
  $(pkg-config --libs libngtcp2 libngtcp2_crypto_ossl libnghttp3 openssl) \
  -o "${WORK}/hy2_tun_probe"

openssl req -x509 -newkey ec -pkeyopt ec_paramgen_curve:prime256v1 -nodes -days 2 \
  -keyout "${WORK}/key.pem" -out "${WORK}/cert.pem" -subj "/CN=localhost" \
  -addext "subjectAltName=DNS:localhost" 2>/dev/null
PORT="${HY2_PORT:-$((20000 + RANDOM % 20000))}"
cat > "${WORK}/server.yaml" <<EOF
listen: 127.0.0.1:${PORT}
tls: { cert: ${WORK}/cert.pem, key: ${WORK}/key.pem, sniGuard: disable }
auth: { type: password, password: probe-secret }
EOF
"${SERVER}" server -c "${WORK}/server.yaml" > "${WORK}/server.log" 2>&1 &
SERVER_PID=$!
sleep 1
kill -0 "${SERVER_PID}" || { cat "${WORK}/server.log"; exit 1; }
HY2_PORT="${PORT}" HY2_AUTH=probe-secret HY2_CA="${WORK}/cert.pem" "${WORK}/hy2_tun_probe"
