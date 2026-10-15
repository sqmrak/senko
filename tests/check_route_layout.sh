#!/usr/bin/env bash
# compile route_layout_check.c for every shipped slice. the file is all
# compile time assertions, so a layout that drifts from the sdk headers fails
# here instead of on a device.
set -euo pipefail

THEOS="${THEOS:?set THEOS to theos root}"
TC="${SENKO_TC:-${THEOS}/toolchain/linux/iphone/bin}"
SDK_V7="${SENKO_SDK_V7:?set SENKO_SDK_V7 to the armv7 sdk}"
SDK_V64="${SENKO_SDK_V64:?set SENKO_SDK_V64 to the arm64 sdk}"

HERE="$(cd "$(dirname "$0")" && pwd)"
CORE="${HERE}/../daemon/core"
OUT="$(mktemp -d)"
trap 'rm -rf "${OUT}"' EXIT

[ -x "${TC}/clang" ] || { echo "no clang at ${TC}/clang, set SENKO_TC" >&2; exit 1; }
[ -d "${SDK_V7}" ] || { echo "no armv7 sdk at ${SDK_V7}" >&2; exit 1; }
[ -d "${SDK_V64}" ] || { echo "no arm64 sdk at ${SDK_V64}" >&2; exit 1; }

check() {
  local arch="$1" target="$2" sdk="$3" minver="$4"
  # _DARWIN_C_SOURCE: -std=c99 hides the BSD typedefs net/if_dl.h is written in
  "${TC}/clang" -target "${target}" -arch "${arch}" -isysroot "${sdk}" \
    -miphoneos-version-min="${minver}" -std=c99 -D_DARWIN_C_SOURCE -Wall -Wextra \
    -I"${CORE}" -c "${HERE}/route_layout_check.c" -o "${OUT}/layout-${arch}.o"
  echo "route message layout matches the sdk: ${arch}"
}

check armv7 arm-apple-darwin11 "${SDK_V7}" 4.3
check arm64 arm64-apple-darwin "${SDK_V64}" 7.0
