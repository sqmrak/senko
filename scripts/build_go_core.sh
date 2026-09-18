#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SRC="${SENKO_GO_CORE_SRC:?set SENKO_GO_CORE_SRC to the pinned go core source}"
GO_BIN="${SENKO_GO:?set SENKO_GO to the Go executable}"
TC="${SENKO_TC:?set SENKO_TC to the iOS toolchain bin directory}"
SDK="${SENKO_SDK_V64:?set SENKO_SDK_V64 to the arm64 iPhoneOS SDK}"
OUT_INPUT="${1:?usage: build_go_core.sh output-path}"
PINNED_COMMIT="2ec1657c03a4c368bfabc31e04df0d2c3f1d50a5"

if [[ "$(${GO_BIN} version)" != go\ version\ go1.27.1\ * ]]; then
  echo "go core requires the pinned Go 1.27.1 toolchain" >&2
  exit 1
fi
if [[ "$(git -C "${SRC}" rev-parse HEAD)" != "${PINNED_COMMIT}" ]]; then
  echo "go core source must be at ${PINNED_COMMIT}" >&2
  exit 1
fi
if [[ ! -f "${SRC}/LICENSE" ]]; then
  echo "go core source license is missing" >&2
  exit 1
fi

# main/distro/all links every xray-core protocol, transport, and app,
# including vmess, trojan, shadowsocks, wireguard, kcp, and the full
# commander/observatory/reverse/metrics/stats stack. go_config.c only ever
# emits a tun inbound and a vless/socks/http/freedom/blackhole outbound set,
# so main/distro/senko registers only that subset instead
DISTRO_PATCH="${ROOT}/scripts/patches/go-core-trim-distro.patch"
if git -C "${SRC}" apply --reverse --check "${DISTRO_PATCH}" 2>/dev/null; then
  : # already applied to this checkout
elif git -C "${SRC}" apply --check "${DISTRO_PATCH}" 2>/dev/null; then
  git -C "${SRC}" apply "${DISTRO_PATCH}"
else
  echo "go-core-trim-distro.patch no longer applies to ${SRC}" >&2
  exit 1
fi

mkdir -p "$(dirname "${OUT_INPUT}")"
OUT="$(cd "$(dirname "${OUT_INPUT}")" && pwd)/$(basename "${OUT_INPUT}")"
CC_CMD="${TC}/clang -target arm64-apple-darwin -B ${TC} -isysroot ${SDK} -miphoneos-version-min=12.0"
(
  cd "${SRC}"
  env GOOS=ios GOARCH=arm64 CGO_ENABLED=1 \
    CC="${CC_CMD}" \
    CGO_CFLAGS="-isysroot ${SDK} -miphoneos-version-min=12.0" \
    CGO_LDFLAGS="-isysroot ${SDK} -miphoneos-version-min=12.0" \
    "${GO_BIN}" build -buildvcs=false -trimpath \
      -ldflags='-s -w -buildid=' -o "${OUT}" ./main
)
"${TC}/ldid" -S "${OUT}"

build_info="$(${TC}/otool -l "${OUT}")"
if ! grep -A5 LC_BUILD_VERSION <<<"${build_info}" | grep -q 'platform 2'; then
  echo "go core is not an iPhoneOS binary" >&2
  exit 1
fi
if ! grep -A5 LC_BUILD_VERSION <<<"${build_info}" | grep -q 'minos 12.0'; then
  echo "go core deployment target is not iOS 12.0" >&2
  exit 1
fi
file "${OUT}"
