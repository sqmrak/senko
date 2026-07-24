#!/usr/bin/env bash
# build static tls archives because the hook has no device-side dependency
set -euo pipefail

THEOS="${THEOS:?set THEOS to theos root}"
TC="${SENKO_TC:-${THEOS}/toolchain/linux/iphone/bin}"
VERSION="${MBEDTLS_VERSION:-3.6.4}"
TAG="mbedtls-${VERSION}"
OUT="${1:-${SENKO_MBED:?set SENKO_MBED to the mbedtls output directory}}"
SRC="${SENKO_MBED_SRC:?set SENKO_MBED_SRC to the mbedtls source directory}"
SDK_V7="${SENKO_SDK_V7:?set SENKO_SDK_V7 to the armv7 sdk}"
SDK_V64="${SENKO_SDK_V64:?set SENKO_SDK_V64 to the arm64 sdk}"

if [ ! -d "${SRC}/.git" ]; then
  mkdir -p "$(dirname "${SRC}")"
  git clone --depth 1 --branch "${TAG}" https://github.com/Mbed-TLS/mbedtls.git "${SRC}"
fi

git -C "${SRC}" submodule update --init --depth 1

build_arch() {
  local name="$1" triple="$2" sdk="$3" arch="$4" min="$5"
  local build="${SRC}/build-senko-${name}"
  local wrap="${SRC}/wrap-cc-${name}"
  rm -rf "${build}"
  mkdir -p "${build}"

  # cmake Darwin mode injects -mmacosx-version-min; wrap clang for iOS
  cat > "${wrap}" <<EOF
#!/bin/sh
exec "${TC}/clang" -target "${triple}" -B "${TC}" \
  -arch "${arch}" -miphoneos-version-min="${min}" -isysroot "${sdk}" \
  -fPIC -O2 -DMBEDTLS_PLATFORM_MS_TIME_ALT "\$@"
EOF
  chmod +x "${wrap}"

  cmake -S "${SRC}" -B "${build}" \
    -DCMAKE_SYSTEM_NAME=Generic \
    -DCMAKE_C_COMPILER="${wrap}" \
    -DCMAKE_C_COMPILER_WORKS=1 \
    -DCMAKE_AR="${TC}/llvm-ar" \
    -DCMAKE_RANLIB="${TC}/llvm-ranlib" \
    -DCMAKE_TRY_COMPILE_TARGET_TYPE=STATIC_LIBRARY \
    -DENABLE_PROGRAMS=OFF -DENABLE_TESTING=OFF \
    -DUSE_SHARED_MBEDTLS_LIBRARY=OFF \
    -DMBEDTLS_FATAL_WARNINGS=OFF

  cmake --build "${build}" --target mbedtls mbedx509 mbedcrypto --parallel "$(nproc)"
  for lib in mbedtls mbedx509 mbedcrypto; do
    cp "${build}/library/lib${lib}.a" "${OUT}/lib/lib${lib}-${name}.a"
  done
}

rm -rf "${OUT}"
mkdir -p "${OUT}/lib"
cp -a "${SRC}/include" "${OUT}/include"

build_arch armv7 arm-apple-darwin11 "${SDK_V7}" armv7 5.0
build_arch arm64 arm64-apple-darwin "${SDK_V64}" arm64 7.0

echo "mbedtls static archives ready: ${OUT}"
ls -lh "${OUT}/lib/"
