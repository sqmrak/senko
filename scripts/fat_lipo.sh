#!/usr/bin/env bash
# merge mach-o slices and fail before producing a partial binary
set -euo pipefail
THEOS="${THEOS:?set THEOS to theos root}"
TC="${SENKO_TC:-${THEOS}/toolchain/linux/iphone/bin}"
LIPO="${SENKO_LIPO:-${TC}/lipo}"
OUT="$1"
shift
"${LIPO}" -create "$@" -output "${OUT}"
"${LIPO}" -info "${OUT}"
