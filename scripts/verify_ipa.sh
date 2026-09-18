#!/usr/bin/env bash
set -euo pipefail

IPA="${1:-}"
if [ -z "${IPA}" ] || [ ! -f "${IPA}" ]; then
    echo "usage: $0 package.ipa" >&2
    exit 2
fi

WORK="$(mktemp -d)"
trap 'rm -rf "${WORK}"' EXIT
bsdtar -xf "${IPA}" -C "${WORK}"
APP="${WORK}/Payload/Senko.app"

[ -x "${APP}/senko" ] || { echo "missing app executable" >&2; exit 1; }
[ -f "${APP}/Info.plist" ] || { echo "missing app Info.plist" >&2; exit 1; }
[ -x "${APP}/PlugIns/SenkoTunnel.appex/SenkoTunnel" ] || {
    echo "missing packet tunnel extension" >&2
    exit 1
}
grep -q '<string>2.1.0-stable</string>' "${APP}/Info.plist" || {
    echo "app version missing" >&2
    exit 1
}

echo "verify_ipa: ok ${IPA}"
