#!/usr/bin/env bash
# static OpenSSL for the iOS 5 armv7 daemon slice
set -euo pipefail

THEOS="${THEOS:?set THEOS to theos root}"
PREFIX="${SENKO_OSSL_V7:?set SENKO_OSSL_V7 to the armv7 openssl prefix}"
BLD="${SENKO_OPENSSL_BLD7:-${PREFIX}/build-armv7-ios5}"
TC="${SENKO_TC:-${THEOS}/toolchain/linux/iphone/bin}"
ARMV7_CLANG="${BLD}/armv7-clang"
SDK="${SENKO_SDK_V7:?set SENKO_SDK_V7 to the armv7 sdk}"
MARKER="${PREFIX}/.senko-ios-min-5.0"

if [[ -f "${PREFIX}/lib/libssl.a" && -f "${PREFIX}/lib/libcrypto.a" &&
      -f "${MARKER}" ]]; then
  echo "openssl armv7 ios5 already at ${PREFIX}"
  exit 0
fi

SRC="${SENKO_OPENSSL_SRC:?set SENKO_OPENSSL_SRC to the openssl source directory}"
[[ -f "${SRC}/Configure" ]] || { echo "missing ${SRC}/Configure" >&2; exit 1; }

rm -rf "${BLD}"
mkdir -p "${BLD}"
cat > "${ARMV7_CLANG}" <<EOF
#!/bin/sh
exec ${TC}/clang -target arm-apple-darwin11 -B ${TC} "\$@"
EOF
chmod +x "${ARMV7_CLANG}"

CROSS_TOP="${SENKO_OPENSSL_CROSS_TOP7:-${BLD}/cross-top-armv7}"
mkdir -p "${CROSS_TOP}/SDKs"
ln -sfn "${SDK}" "${CROSS_TOP}/SDKs/iPhoneOS.sdk"
export CROSS_TOP CROSS_SDK="iPhoneOS.sdk"
export PATH="${TC}:${PATH}"

cd "${BLD}"
"${SRC}/Configure" ios-cross \
  "CC=${ARMV7_CLANG}" "AR=${TC}/llvm-ar" "RANLIB=${TC}/llvm-ranlib" \
  no-asm no-shared no-dso no-tests no-engine no-ui-console \
  -DBROKEN_CLANG_ATOMICS -O2 -fno-strict-aliasing \
  --prefix="${PREFIX}" -miphoneos-version-min=5.0 -isysroot "${SDK}"
make -j"$(nproc)" build_libs

mkdir -p "${PREFIX}/lib" "${PREFIX}/include"
cp -a libssl.a libcrypto.a "${PREFIX}/lib/"
cp -a include/openssl "${PREFIX}/include/" 2>/dev/null || true
mkdir -p "${PREFIX}/include/openssl"
cp -rn "${SRC}/include/openssl/." "${PREFIX}/include/openssl/" 2>/dev/null || true
cp -a include/openssl/configuration.h "${PREFIX}/include/openssl/" 2>/dev/null || true
touch "${MARKER}"
echo "openssl armv7 ios5 installed: ${PREFIX}"
