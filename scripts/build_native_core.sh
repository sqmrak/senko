#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SRC="${SENKO_CORE_SRC:?set SENKO_CORE_SRC to the pinned senko-core source}"
GO_BIN="${SENKO_GO:?set SENKO_GO to the Go executable}"
TC="${SENKO_TC:?set SENKO_TC to the iOS toolchain bin directory}"
SDK="${SENKO_SDK_V64:?set SENKO_SDK_V64 to the arm64 iPhoneOS SDK}"
MIN_IOS="${SENKO_NATIVE_MIN_IOS:-9.0}"
OUT_INPUT="${1:?usage: build_native_core.sh output-path}"
PINNED_COMMIT="2ec1657c03a4c368bfabc31e04df0d2c3f1d50a5"

if [[ "$(${GO_BIN} version)" != go\ version\ go1.27.1\ * ]]; then
  echo "native core requires the pinned Go 1.27.1 toolchain" >&2
  exit 1
fi
if [[ "$(git -C "${SRC}" rev-parse HEAD)" != "${PINNED_COMMIT}" ]]; then
  echo "senko-core source must be at ${PINNED_COMMIT}" >&2
  exit 1
fi
if [[ ! -f "${SRC}/LICENSE" ]]; then
  echo "senko-core source license is missing" >&2
  exit 1
fi

bash "${ROOT}/scripts/ensure_senko_core_distro.sh" "${SRC}"

mkdir -p "$(dirname "${OUT_INPUT}")"
OUT="$(cd "$(dirname "${OUT_INPUT}")" && pwd)/$(basename "${OUT_INPUT}")"
AR_OVERLAY="$(mktemp -d "${TMPDIR:-/tmp}/senko-native-ar.XXXXXX")"
RT_OVERLAY="$(mktemp -d "${TMPDIR:-/tmp}/senko-native-rt.XXXXXX")"
trap 'rmdir "${AR_OVERLAY}" 2>/dev/null || true; rm -rf "${RT_OVERLAY}"' EXIT
ln -s "${ROOT}/scripts/ios-ar-wrapper.sh" "${AR_OVERLAY}/ar"
cp "${ROOT}/native/go/go.mod" "${RT_OVERLAY}/go.mod"
cp "${ROOT}/native/go/go.sum" "${RT_OVERLAY}/go.sum"
(
  cd "${ROOT}/native/go"
  "${GO_BIN}" mod edit -modfile "${RT_OVERLAY}/go.mod" \
    -replace "github.com/xtls/xray-core=${SRC}"
)
# go 1.27 spins on CNTVCT_EL0, which ios before the A11 traps with SIGILL
GOROOT_SRC="$(${GO_BIN} env GOROOT)/src"
mkdir -p "${RT_OVERLAY}/src/runtime"
cp "${GOROOT_SRC}/runtime/asm_arm64.s" "${RT_OVERLAY}/src/runtime/asm_arm64.s"
patch -s -p1 -d "${RT_OVERLAY}" < "${ROOT}/scripts/patches/go-runtime-procyield-ios.patch"
printf '{"Replace":{"%s":"%s"}}\n' "${GOROOT_SRC}/runtime/asm_arm64.s" \
  "${RT_OVERLAY}/src/runtime/asm_arm64.s" > "${RT_OVERLAY}/overlay.json"
bash "${ROOT}/scripts/prepare_core_overlay.sh" "${SRC}" "${RT_OVERLAY}"
CC_CMD="${TC}/clang -target arm64-apple-darwin -B ${AR_OVERLAY} -B ${TC} -isysroot ${SDK} -miphoneos-version-min=${MIN_IOS}"
(
  cd "${ROOT}/native/go"
  env GOOS=ios GOARCH=arm64 CGO_ENABLED=1 \
    CC="${CC_CMD}" \
    SENKO_LLVM_AR="${TC}/llvm-ar" \
    CGO_CFLAGS="-isysroot ${SDK} -miphoneos-version-min=${MIN_IOS}" \
    CGO_LDFLAGS="-isysroot ${SDK} -miphoneos-version-min=${MIN_IOS}" \
    "${GO_BIN}" build -modfile "${RT_OVERLAY}/go.mod" -overlay "${RT_OVERLAY}/overlay.json" -tags senko_trim -buildvcs=false -trimpath -buildmode=c-archive \
      -ldflags='-s -w -buildid=' -o "${OUT}" .
)
"${TC}/ranlib" "${OUT}" 2>/dev/null || true
file "${OUT}"
