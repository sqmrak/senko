#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SRC="${SENKO_GO_CORE_SRC:?set SENKO_GO_CORE_SRC to the pinned go core source}"
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
  echo "go core source must be at ${PINNED_COMMIT}" >&2
  exit 1
fi
if [[ ! -f "${SRC}/LICENSE" ]]; then
  echo "go core source license is missing" >&2
  exit 1
fi

if [[ ! -f "${SRC}/main/distro/senko/senko.go" ]]; then
  DISTRO_PATCH="${ROOT}/scripts/patches/go-core-trim-distro.patch"
  if ! git -C "${SRC}" apply --check "${DISTRO_PATCH}"; then
    echo "the trimmed senko distro is missing and the maintained patch does not apply" >&2
    exit 1
  fi
  git -C "${SRC}" apply "${DISTRO_PATCH}"
fi

mkdir -p "$(dirname "${OUT_INPUT}")"
OUT="$(cd "$(dirname "${OUT_INPUT}")" && pwd)/$(basename "${OUT_INPUT}")"
AR_OVERLAY="$(mktemp -d "${TMPDIR:-/tmp}/senko-native-ar.XXXXXX")"
trap 'rmdir "${AR_OVERLAY}" 2>/dev/null || true' EXIT
ln -s "${ROOT}/scripts/ios-ar-wrapper.sh" "${AR_OVERLAY}/ar"
CC_CMD="${TC}/clang -target arm64-apple-darwin -B ${AR_OVERLAY} -B ${TC} -isysroot ${SDK} -miphoneos-version-min=${MIN_IOS}"
(
  cd "${ROOT}/native/go"
  env GOOS=ios GOARCH=arm64 CGO_ENABLED=1 \
    CC="${CC_CMD}" \
    SENKO_LLVM_AR="${TC}/llvm-ar" \
    CGO_CFLAGS="-isysroot ${SDK} -miphoneos-version-min=${MIN_IOS}" \
    CGO_LDFLAGS="-isysroot ${SDK} -miphoneos-version-min=${MIN_IOS}" \
    "${GO_BIN}" build -buildvcs=false -trimpath -buildmode=c-archive \
      -ldflags='-s -w -buildid=' -o "${OUT}" .
)
"${TC}/ranlib" "${OUT}" 2>/dev/null || true
file "${OUT}"
