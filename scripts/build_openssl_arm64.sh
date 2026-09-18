#!/usr/bin/env bash
# static openssl 3.x for ios arm64 (senkod tls stack)
set -euo pipefail

THEOS="${THEOS:?set THEOS to theos root}"
PREFIX="${SENKO_OSSL_PREFIX:-${SENKO_OSSL_V64:?set SENKO_OSSL_V64 to the arm64 openssl prefix}}"
OSSL_ARCH="${SENKO_OSSL_ARCH:-arm64}"
OSSL_TRIPLE="${SENKO_OSSL_TRIPLE:-arm64-apple-darwin}"
BLD="${SENKO_OSSL_BLD:-${SENKO_OPENSSL_BLD64:-${PREFIX}/build-${OSSL_ARCH}}}"
TC="${SENKO_TC:-${THEOS}/toolchain/linux/iphone/bin}"
ARM64_CLANG="${SENKO_ARM64_CLANG:-${BLD}/arm64-clang}"
SDK="${SENKO_OSSL_SDK:-${SENKO_SDK_V64:?set SENKO_SDK_V64 to the arm64 sdk}}"

if [[ -f "${PREFIX}/lib/libssl.a" && -f "${PREFIX}/lib/libcrypto.a" ]]; then
  echo "openssl arm64 already at ${PREFIX}"
  exit 0
fi

SRC="${SENKO_OPENSSL_SRC:?set SENKO_OPENSSL_SRC to the openssl source directory}"

if [[ ! -f "${SRC}/Configure" ]]; then
  echo "missing ${SRC} - clone openssl first" >&2
  exit 1
fi

rm -rf "${BLD}"
mkdir -p "${BLD}"

cat > "${ARM64_CLANG}" <<EOF
#!/usr/bin/env bash
# ios64-xcrun hardcodes its own "-arch arm64" in Configurations/15-ios.conf;
# forwarding that alongside ours made clang emit a fat arm64+arm64e object,
# and ld64 could not pull the arm64e slice back out of the resulting archive
args=()
skip=0
for a in "\$@"; do
  if [ "\${skip}" = 1 ]; then skip=0; continue; fi
  if [ "\${a}" = "-arch" ]; then skip=1; continue; fi
  args+=("\${a}")
done
exec ${TC}/clang -target ${OSSL_TRIPLE} -B ${TC} -arch ${OSSL_ARCH} "\${args[@]}"
EOF
chmod +x "${ARM64_CLANG}"

# openssl ios64-cross looks up CROSS_TOP/SDKs/CROSS_SDK
CROSS_TOP="${SENKO_OPENSSL_CROSS_TOP:-${BLD}/cross-top-arm64}"
mkdir -p "${CROSS_TOP}/SDKs"
ln -sfn "${SDK}" "${CROSS_TOP}/SDKs/iPhoneOS.sdk"
export CROSS_TOP
export CROSS_SDK="iPhoneOS.sdk"
export PATH="${TC}:${PATH}"

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
mkdir -p "${PREFIX}/include/openssl"
cp -rn "${SRC}/include/openssl/." "${PREFIX}/include/openssl/" 2>/dev/null || true
cp -a "${BLD}/include/openssl/configuration.h" "${PREFIX}/include/openssl/" 2>/dev/null || true

echo "openssl ${OSSL_ARCH} installed: ${PREFIX}"
ls -lh "${PREFIX}/lib/libssl.a" "${PREFIX}/lib/libcrypto.a"
