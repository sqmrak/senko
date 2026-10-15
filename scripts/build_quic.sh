#!/usr/bin/env bash
# static ngtcp2 and nghttp3 for the hysteria2 client inside senkod.
# output: ${SENKO_QUIC}/<armv7|arm64>/{lib,include}
set -euo pipefail

THEOS="${THEOS:?set THEOS to theos root}"
TC="${SENKO_TC:-${THEOS}/toolchain/linux/iphone/bin}"
NGTCP2="${SENKO_NGTCP2_SRC:?set SENKO_NGTCP2_SRC to the ngtcp2 source directory}"
NGHTTP3="${SENKO_NGHTTP3_SRC:?set SENKO_NGHTTP3_SRC to the nghttp3 source directory}"
OUT="${SENKO_QUIC:?set SENKO_QUIC to the quic output directory}"
SDK_V7="${SENKO_SDK_V7:?set SENKO_SDK_V7 to the armv7 sdk}"
SDK_V64="${SENKO_SDK_V64:?set SENKO_SDK_V64 to the arm64 sdk}"
OSSL_V7="${SENKO_OSSL_V7:?set SENKO_OSSL_V7 to the armv7 openssl prefix}"
OSSL_V64="${SENKO_OSSL_V64:?set SENKO_OSSL_V64 to the arm64 openssl prefix}"

# a rebuild only happens when a source tree or this script changed
MARKER="${OUT}/.senko-quic"
STAMP="$(basename "${NGTCP2}") $(basename "${NGHTTP3}") $(sha256sum "$0" | cut -d' ' -f1)"
if [[ -f "${MARKER}" && "$(cat "${MARKER}")" == "${STAMP}" &&
      -f "${OUT}/armv7/lib/libngtcp2.a" && -f "${OUT}/armv7/lib/libnghttp3.a" &&
      -f "${OUT}/arm64/lib/libngtcp2.a" && -f "${OUT}/arm64/lib/libnghttp3.a" ]]; then
  echo "quic archives already at ${OUT}"
  exit 0
fi

for dir in "${NGTCP2}/lib" "${NGHTTP3}/lib"; do
  [[ -d "${dir}" ]] || { echo "missing ${dir}" >&2; exit 1; }
done

# ngtcp2_crypto_ossl needs SSL_set_quic_tls_cbs, which only openssl 3.5 has
check_openssl() {
  local ossl="$1"
  # grep -q would close the pipe early and trip pipefail on llvm-nm
  if ! "${TC}/llvm-nm" "${ossl}/lib/libssl.a" 2>/dev/null | grep ' T _SSL_set_quic_tls_cbs' >/dev/null; then
    echo "${ossl} has no SSL_set_quic_tls_cbs, build openssl 3.5 or newer" >&2
    exit 1
  fi
}

# the sources include <config.h> only under HAVE_CONFIG_H, so the few
# feature macros go on the command line; ios 5 has neither memset_s nor
# explicit_bzero, and ngtcp2 falls back to a volatile memset pointer there
build_arch() {
  local name="$1" triple="$2" sdk="$3" arch="$4" min="$5" ossl="$6"
  local dir="${OUT}/${name}"
  local obj="${dir}/obj"
  check_openssl "${ossl}"
  rm -rf "${dir}"
  mkdir -p "${obj}/ngtcp2" "${obj}/nghttp3" "${dir}/lib"

  local flags=(-target "${triple}" -B "${TC}" -arch "${arch}"
    -miphoneos-version-min="${min}" -isysroot "${sdk}" -std=gnu11 -O2
    -fno-strict-aliasing -DNGTCP2_STATICLIB -DNGHTTP3_STATICLIB
    -DHAVE_ARPA_INET_H -DHAVE_NETINET_IN_H -DHAVE_UNISTD_H
    -D_DARWIN_C_SOURCE
    -I"${NGTCP2}/lib" -I"${NGTCP2}/lib/includes" -I"${NGTCP2}/crypto"
    -I"${NGTCP2}/crypto/includes" -I"${NGHTTP3}/lib" -I"${NGHTTP3}/lib/includes"
    -I"${ossl}/include")

  local src
  for src in "${NGTCP2}"/lib/*.c "${NGTCP2}/crypto/shared.c" "${NGTCP2}/crypto/ossl/ossl.c"; do
    "${TC}/clang" "${flags[@]}" -c "${src}" -o "${obj}/ngtcp2/$(basename "${src}" .c).o"
  done
  # nghttp3 vendors sfparse for the priority header
  for src in "${NGHTTP3}"/lib/*.c "${NGHTTP3}/lib/sfparse/sfparse.c"; do
    "${TC}/clang" "${flags[@]}" -c "${src}" -o "${obj}/nghttp3/$(basename "${src}" .c).o"
  done
  rm -f "${dir}/lib/libngtcp2.a" "${dir}/lib/libnghttp3.a"
  "${TC}/llvm-ar" rcs "${dir}/lib/libngtcp2.a" "${obj}"/ngtcp2/*.o
  "${TC}/llvm-ar" rcs "${dir}/lib/libnghttp3.a" "${obj}"/nghttp3/*.o
  rm -rf "${obj}"
  mkdir -p "${dir}/include/ngtcp2" "${dir}/include/nghttp3"
  cp "${NGTCP2}"/lib/includes/ngtcp2/*.h "${dir}/include/ngtcp2/"
  cp "${NGTCP2}/crypto/includes/ngtcp2/ngtcp2_crypto.h" \
     "${NGTCP2}/crypto/includes/ngtcp2/ngtcp2_crypto_ossl.h" "${dir}/include/ngtcp2/"
  cp "${NGHTTP3}"/lib/includes/nghttp3/*.h "${dir}/include/nghttp3/"
}

build_arch armv7 arm-apple-darwin11 "${SDK_V7}" armv7 4.3 "${OSSL_V7}"
build_arch arm64 arm64-apple-darwin "${SDK_V64}" arm64 7.0 "${OSSL_V64}"

echo "${STAMP}" > "${MARKER}"
echo "quic archives installed: ${OUT}"
