#!/usr/bin/env bash
# fetch root ca files during builds so runtime stays offline
# preserve cached roots when the source host is unreachable
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
OUT="${ROOT}/senkotlsfix/roots"
mkdir -p "${OUT}"

fetch_one() {
    local url="$1"
    local dest="$2"
    local required="${3:-0}"
    if curl -fsSL --connect-timeout 12 --max-time 30 "${url}" -o "${dest}.tmp"; then
        mv "${dest}.tmp" "${dest}"
        echo "ok ${dest}"
        return 0
    fi
    rm -f "${dest}.tmp"
    if [ -s "${dest}" ]; then
        echo "cached ${dest} (fetch failed)"
        return 0
    fi
    if [ "${required}" = "1" ]; then
        echo "missing ${dest} (fetch failed, no cache)" >&2
        return 1
    fi
    echo "skip ${dest} (optional, unavailable)"
    return 0
}

fetch_one "https://letsencrypt.org/certs/isrgrootx1.pem" \
          "${OUT}/isrgrootx1.pem" 1
fetch_one "https://letsencrypt.org/certs/isrg-root-x2.pem" \
          "${OUT}/isrgrootx2.pem" 0
fetch_one "https://cacerts.digicert.com/DigiCertGlobalRootG2.crt.pem" \
          "${OUT}/digicertg2.pem" 1
fetch_one "https://cacerts.digicert.com/DigiCertGlobalRootCA.crt.pem" \
          "${OUT}/digicertroot.pem" 0
fetch_one "https://cacerts.digicert.com/BaltimoreCyberTrustRoot.crt.pem" \
          "${OUT}/baltimore.pem" 0
fetch_one "https://letsencrypt.org/certs/lets-encrypt-r3.pem" \
          "${OUT}/letsencryptr3.pem" 0
fetch_one "https://letsencrypt.org/certs/trustid-x3-root.pem" \
          "${OUT}/dstx3.pem" 0
fetch_one "https://letsencrypt.org/certs/isrg-root-x1-cross-signed.pem" \
          "${OUT}/isrgx1cross.pem" 0

count=$(ls -1 "${OUT}"/*.pem 2>/dev/null | wc -l)
if [ "${count}" -lt 2 ]; then
    echo "need at least 2 root pem files" >&2
    exit 1
fi
echo "roots ready: ${count} file(s)"
