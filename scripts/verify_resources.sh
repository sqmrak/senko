#!/usr/bin/env bash
# verify every url used by builds, runtime probes, or diagnostics
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
TIMEOUT=12
FAIL=0

check() {
    local tier="$1"
    local url="$2"
    local note="$3"
    local code
    code=$(curl -sS -o /dev/null -w "%{http_code}" --connect-timeout 8 --max-time "${TIMEOUT}" "${url}" 2>/dev/null || echo "fail")
    local ok=0
    case "${code}" in
        2*|3*) ok=1 ;;
    esac
    if [ "${tier}" = "offline" ]; then
        echo "[offline] ${note}"
        return 0
    fi
    if [ "${ok}" = "1" ]; then
        echo "[ok ${tier}] ${code} ${url}  (${note})"
    else
        echo "[FAIL ${tier}] ${code} ${url}  (${note})" >&2
        if [ "${tier}" = "build" ] || [ "${tier}" = "must" ]; then
            FAIL=1
        fi
    fi
}

if [ "${SENKO_OFFLINE:-0}" = "1" ]; then
    echo "==> network checks skipped (offline build)"
else
    echo "==> build-time (must have cache offline if fail)"
    check build "https://letsencrypt.org/certs/isrgrootx1.pem" "tlsfix root bundle"
    check build "https://cacerts.digicert.com/DigiCertGlobalRootG2.crt.pem" "tlsfix root bundle"

    echo "==> https suite must (should work in rf without vpn)"
    check must "https://example.com/" "baseline tls"
    check must "http://example.com/" "baseline http"
    check must "https://icanhazip.com/" "ip check"
    check must "http://ifconfig.me/ip" "ip check"
    check must "https://ya.ru/" "ru local"
    check must "https://api.ipify.org/" "ip json"

    echo "==> https suite best"
    check best "https://www.google.com/generate_204" "captive portal probe"

    echo "==> rf blocked without vpn (fail expected, not a build error)"
    check blocked "https://x.com/" "roskomnadzor"
    check blocked "https://www.instagram.com/" "roskomnadzor"
    check blocked "https://www.youtube.com/" "roskomnadzor"
    check blocked "https://discord.com/" "roskomnadzor"
fi

echo "==> packaged offline assets"
if [ -f "${ROOT}/senkotlsfix/cacert.pem" ]; then
    n=$(grep -c 'BEGIN CERTIFICATE' "${ROOT}/senkotlsfix/cacert.pem" || true)
    [ "${n}" -gt 100 ] || { echo "cacert.pem too small (${n} certs)" >&2; FAIL=1; }
    echo "[offline] cacert.pem ${n} certs"
else
    echo "missing senkotlsfix/cacert.pem" >&2
    FAIL=1
fi
for f in "${ROOT}/senkotlsfix/roots/"*.pem; do
    [ -f "${f}" ] || continue
    openssl x509 -in "${f}" -noout -subject 2>/dev/null || { echo "bad pem ${f}" >&2; FAIL=1; }
    echo "[offline] $(basename "${f}") valid"
done

if [ "${FAIL}" != "0" ]; then
    echo "verify_resources: hard failures in build/must tier" >&2
    exit 1
fi
echo "verify_resources: ok"
