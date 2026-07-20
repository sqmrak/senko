#!/usr/bin/env bash
# merge mach-o slices and fail before producing a partial binary
set -euo pipefail
TC="${HOME}/theos/toolchain/linux/iphone/bin"
LIPO="${TC}/lipo"
OUT="$1"
shift
"${LIPO}" -create "$@" -output "${OUT}"
"${LIPO}" -info "${OUT}"
