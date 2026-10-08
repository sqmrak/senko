#!/usr/bin/env bash
# runs tests/hy2_probe.c against a real hysteria2 server, so the engine is
# checked against the reference implementation and not against this repo's idea
# of the protocol. needs the hysteria binary (HY2_SERVER) and the host
# ngtcp2, nghttp3 and openssl development files; not part of `make test`
set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
SERVER="${HY2_SERVER:?set HY2_SERVER to the hysteria server binary}"
WORK="$(mktemp -d)"
trap 'kill ${SERVER_PID:-0} 2>/dev/null || true; rm -rf "${WORK}"' EXIT
CC="${CC:-cc}"

"${CC}" -std=gnu99 -Wall -Wextra -O2 -pthread ${HY2_CFLAGS:-} \
  -I"${HERE}/../daemon" -I"${HERE}/../daemon/core" \
  $(pkg-config --cflags libngtcp2 libngtcp2_crypto_ossl libnghttp3 openssl) \
  "${HERE}/hy2_probe.c" "${HERE}/../daemon/hy2_engine.c" \
  "${HERE}/../daemon/core/hy2_proto.c" "${HERE}/../daemon/core/blake2b256.c" \
  "${HERE}/../daemon/core/senko_time.c" \
  $(pkg-config --libs libngtcp2 libngtcp2_crypto_ossl libnghttp3 openssl) \
  -o "${WORK}/hy2_probe"

openssl req -x509 -newkey ec -pkeyopt ec_paramgen_curve:prime256v1 -nodes -days 2 \
  -keyout "${WORK}/key.pem" -out "${WORK}/cert.pem" -subj "/CN=localhost" \
  -addext "subjectAltName=DNS:localhost" 2>/dev/null
PIN="$(openssl x509 -in "${WORK}/cert.pem" -outform DER | openssl dgst -sha256 -hex | awk '{print $NF}')"

PORT="${HY2_PORT:-$((20000 + RANDOM % 20000))}"
write_config() {
  local obfs="$1"
  {
    echo "listen: 127.0.0.1:${PORT}"
    echo "tls: { cert: ${WORK}/cert.pem, key: ${WORK}/key.pem, sniGuard: disable }"
    echo "auth: { type: password, password: probe-secret }"
    if [[ -n "${obfs}" ]]; then
      echo "obfs: { type: salamander, salamander: { password: ${obfs} } }"
    fi
  } > "${WORK}/server.yaml"
}
start_server() {
  "${SERVER}" server -c "${WORK}/server.yaml" > "${WORK}/server.log" 2>&1 &
  SERVER_PID=$!
  sleep 1
  kill -0 "${SERVER_PID}" || { cat "${WORK}/server.log"; exit 1; }
}
stop_server() {
  kill "${SERVER_PID}" 2>/dev/null || true
  wait "${SERVER_PID}" 2>/dev/null || true
}
run() {
  env HY2_PORT="${PORT}" HY2_CA="${WORK}/cert.pem" "$@"
}

echo "== transfer, verified by ca"
write_config ""
start_server
run HY2_AUTH=probe-secret "${WORK}/hy2_probe" transfer

echo "== wrong password"
run HY2_AUTH=wrong "${WORK}/hy2_probe" reject

echo "== wrong server name"
run HY2_AUTH=probe-secret HY2_SNI=other.example "${WORK}/hy2_probe" reject

echo "== transfer, pinned certificate"
run HY2_AUTH=probe-secret HY2_CA=/nonexistent HY2_PIN="${PIN}" "${WORK}/hy2_probe" transfer

echo "== wrong pin"
run HY2_AUTH=probe-secret HY2_PIN="$(printf '00%.0s' $(seq 32))" "${WORK}/hy2_probe" reject
stop_server

echo "== transfer, salamander"
write_config "probe-obfs"
start_server
run HY2_AUTH=probe-secret HY2_OBFS=probe-obfs "${WORK}/hy2_probe" transfer
echo "== salamander key mismatch"
run HY2_AUTH=probe-secret HY2_OBFS=different "${WORK}/hy2_probe" reject
stop_server

echo "== server dies mid flow"
write_config ""
start_server
( run HY2_AUTH=probe-secret "${WORK}/hy2_probe" abort | tee "${WORK}/abort.out" ) &
PROBE_PID=$!
for _ in $(seq 50); do grep -q STREAMING "${WORK}/abort.out" 2>/dev/null && break; sleep 0.1; done
sleep 1
stop_server
wait "${PROBE_PID}"
echo "all hysteria2 probes passed"
