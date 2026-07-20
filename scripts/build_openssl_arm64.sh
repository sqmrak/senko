#!/usr/bin/env bash
# static openssl 3.x for ios arm64 (senkod tls stack)
set -euo pipefail

THEOS="${THEOS:-${HOME}/theos}"
PREFIX="${SENKO_OSSL_V64:-${HOME}/src/openssl-build/install-arm64-o1}"
SRC="${SENKO_OPENSSL_SRC:-${HOME}/src/openssl}"
BLD="${SENKO_OPENSSL_BLD64:-${HOME}/src/openssl-build/arm64-o1}"
TC="${SENKO_TC:-${THEOS}/toolchain/linux/iphone/bin}"
ARM64_CLANG="${SENKO_ARM64_CLANG:-${HOME}/src/arm64-clang}"
SDK="${SENKO_SDK_V64:-${HOME}/sdks/iPhoneOS11.4.sdk}"

if [[ -f "${PREFIX}/lib/libssl.a" && -f "${PREFIX}/lib/libcrypto.a" ]]; then
  echo "openssl arm64 already at ${PREFIX}"
  exit 0
fi

if [[ ! -f "${SRC}/Configure" ]]; then
  echo "missing ${SRC} - clone openssl first" >&2
  exit 1
fi

cat > "${ARM64_CLANG}" <<EOF
#!/bin/sh
exec ${TC}/clang -target arm64-apple-darwin -B ${TC} "\$@"
EOF
chmod +x "${ARM64_CLANG}"

# openssl ios64-cross looks up CROSS_TOP/SDKs/CROSS_SDK
CROSS_TOP="${HOME}/src/openssl-build/cross-top-arm64"
mkdir -p "${CROSS_TOP}/SDKs"
ln -sfn "${SDK}" "${CROSS_TOP}/SDKs/iPhoneOS.sdk"
export CROSS_TOP
export CROSS_SDK="iPhoneOS.sdk"
export PATH="${TC}:${PATH}"

rm -rf "${BLD}"
mkdir -p "${BLD}"
cd "${BLD}"

"${SRC}/Configure" ios64-cross \
  "CC=${ARM64_CLANG}" \
  "AR=${TC}/llvm-ar" \
  "RANLIB=${TC}/llvm-ranlib" \
  no-asm no-shared no-dso no-tests no-engine no-ui-console \
  -DBROKEN_CLANG_ATOMICS -O2 -fno-strict-aliasing \
  --prefix="${PREFIX}" \
  -miphoneos-version-min=7.0 \
  -isysroot "${SDK}"

# libs are enough; openssl CLI often fails to link under linux ld64
if ! make -j"$(nproc)" build_libs; then
  echo "build_libs failed; trying full make then harvesting libs" >&2
  make -j"$(nproc)" || true
fi

if [[ ! -f "${BLD}/libssl.a" || ! -f "${BLD}/libcrypto.a" ]]; then
  echo "openssl arm64 static libs missing after build" >&2
  exit 1
fi

mkdir -p "${PREFIX}/lib" "${PREFIX}/include"
cp -a "${BLD}/libssl.a" "${BLD}/libcrypto.a" "${PREFIX}/lib/"
cp -a "${BLD}/include/openssl" "${PREFIX}/include/" 2>/dev/null || true
rsync -a --ignore-existing "${SRC}/include/openssl/" "${PREFIX}/include/openssl/" 2>/dev/null || true
cp -a "${BLD}/include/openssl/configuration.h" "${PREFIX}/include/openssl/" 2>/dev/null || true

echo "openssl arm64 installed: ${PREFIX}"
ls -lh "${PREFIX}/lib/libssl.a" "${PREFIX}/lib/libcrypto.a"
