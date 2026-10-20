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

if [[ ! "$(${GO_BIN} version)" =~ go\ version\ go1\.[0-9]+ ]]; then
  echo "native core requires a Go 1.x toolchain" >&2
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
trap 'rmdir "${AR_OVERLAY}" 2>/dev/null || true' EXIT
ln -s "${ROOT}/scripts/ios-ar-wrapper.sh" "${AR_OVERLAY}/ar"
CC_CMD="${TC}/clang -target arm64-apple-darwin -B ${AR_OVERLAY} -B ${TC} -isysroot ${SDK} -miphoneos-version-min=${MIN_IOS}"
(
  cd "${ROOT}/native/go"
  "${GO_BIN}" mod edit -replace github.com/xtls/xray-core="${SRC}"
  env GOOS=ios GOARCH=arm64 GOARM64=v8.0 CGO_ENABLED=1 \
    CC="${CC_CMD}" \
    SENKO_LLVM_AR="${TC}/llvm-ar" \
    CGO_CFLAGS="-isysroot ${SDK} -miphoneos-version-min=${MIN_IOS}" \
    CGO_LDFLAGS="-isysroot ${SDK} -miphoneos-version-min=${MIN_IOS}" \
    "${GO_BIN}" build -tags senko_trim -buildvcs=false -trimpath -buildmode=c-archive \
      -ldflags='-s -w -buildid=' -o "${OUT}" .
)
# Patch Apple A7 incompatible CNTVCT_EL0 instruction in Go runtime procyieldAsm
# Opcode for MRS X2, CNTVCT_EL0 is 42 e0 3b d5. Replace with RET (c0 03 5f d6)
python3 -c "
with open('${OUT}', 'r+b') as f:
    data = f.read()
    # Pattern: 42e03bd5 (MRS X2, CNTVCT_EL0)
    target = bytes.fromhex('42e03bd5')
    pos = data.find(target)
    if pos != -1:
        # Patch the entry/read to RET
        f.seek(pos)
        f.write(bytes.fromhex('c0035fd6'))
        print('Successfully patched CNTVCT_EL0 with RET at offset', hex(pos))
    else:
        print('CNTVCT_EL0 pattern not found (already clean)')
" 2>/dev/null || true

"${TC}/ranlib" "${OUT}" 2>/dev/null || true
file "${OUT}"
