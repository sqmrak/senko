#!/usr/bin/env bash
# one archive prevents users from choosing the wrong jailbreak layout
set -euo pipefail

ROOT="$(cd "$(dirname "$0")" && pwd)"
THEOS="${THEOS:?set THEOS to theos root}"
TC="${SENKO_TC:-${THEOS}/toolchain/linux/iphone/bin}"
LIPO="${TC}/lipo"
HOST_AR="${SENKO_HOST_AR:-/usr/bin/ar}"
PKG_VERSION="$(awk -F': ' '$1 == "Version" { print $2; exit }' "${ROOT}/packaging/DEBIAN/control")"
PKG_ARCH="$(awk -F': ' '$1 == "Architecture" { print $2; exit }' "${ROOT}/packaging/DEBIAN/control")"
if [[ ! "${PKG_VERSION}" =~ ^[0-9][0-9A-Za-z.+:~-]*$ ]]; then
  echo "invalid Debian package version: ${PKG_VERSION:-<empty>}" >&2
  exit 1
fi
if [[ "${PKG_ARCH}" != "all" ]]; then
  echo "universal package must use Architecture: all" >&2
  exit 1
fi
OUT="${ROOT}/senko-v${PKG_VERSION}.deb"
# thin armv7 sdk: fat dylib remap is flaky on linux aarch64 hosts
SDK_V7="${SENKO_SDK_V7:?set SENKO_SDK_V7 to the armv7 sdk}"
SDK_V64="${SENKO_SDK_V64:?set SENKO_SDK_V64 to the arm64 sdk}"
CRT_V7="${SENKO_CRT_V7:?set SENKO_CRT_V7 to the armv7 startup object}"
OSSL_V7="${SENKO_OSSL_V7:?set SENKO_OSSL_V7 to the armv7 openssl prefix}"
OSSL_V64="${SENKO_OSSL_V64:?set SENKO_OSSL_V64 to the arm64 openssl prefix}"
# static mbedtls for the tlsfix hook (no device-side dylib)
MBED="${SENKO_MBED:?set SENKO_MBED to the mbedtls output directory}"
GO_CORE_SRC="${SENKO_GO_CORE_SRC:?set SENKO_GO_CORE_SRC to the pinned go core source}"
GO_BIN="${SENKO_GO:?set SENKO_GO to the Go 1.27.1 executable}"
STAGE="${ROOT}/.package-stage"
SLICE="${ROOT}/.build-slices"
# /var stays writable on rootless and rootful systems, so one payload can serve both
JBROOT="/var/jb"
ARM64_ROOT_FLAGS="-DSENKO_ROOTLESS=1"
ARM64_TLSFIX_INSTALL_NAME="/var/jb/usr/lib/senkotlsfix.dylib"
ARM64_VPNICON_INSTALL_NAME="/var/jb/usr/lib/senkovpnicon.dylib"
PAYLOAD="${STAGE}${JBROOT}"

cleanup_build_outputs() {
  rm -rf "${STAGE}" "${SLICE}" "${ROOT}/app/build" "${ROOT}/daemon/build"
  rm -f "${ROOT}/senkotlsfix/senkotlsfix.dylib" \
        "${ROOT}/senkotlsfix/senkotlsfix-armv7.dylib" \
        "${ROOT}/senkotlsfix/senkotlsfix-arm64.dylib"
  env PATH="${SENKO_HOST_PATH:-/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin}" \
    make -C "${ROOT}/tests" clean >/dev/null 2>&1 || true
}

# cleanup on failure too because stale slices can contaminate the next package
trap cleanup_build_outputs EXIT

PLIST_VERSION="$(sed -n '/<key>CFBundleShortVersionString<\/key>/{n;s/.*<string>\(.*\)<\/string>.*/\1/p;q;}' "${ROOT}/app/Info.plist")"
HEADER_VERSION="$(sed -n 's/^#define SENKO_VERSION @"v\([^"]*\)"/\1/p' "${ROOT}/app/app_common.h")"
if [[ "${PLIST_VERSION}" != "${PKG_VERSION}" || "${HEADER_VERSION}" != "${PKG_VERSION}" ]]; then
  echo "version mismatch: package=${PKG_VERSION} plist=${PLIST_VERSION} header=${HEADER_VERSION}" >&2
  exit 1
fi

make_fat() {
  local out="$1"
  shift
  "${LIPO}" -create "$@" -output "${out}"
}

echo "==> host tests"
# keep system compiler first so host tests are not built with the ios clang
HOST_PATH="${SENKO_HOST_PATH:-/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin}"
env PATH="${HOST_PATH}" make -C "${ROOT}/tests" test CC="${CC:-/usr/bin/cc}"

echo "==> fetch tls roots"
bash "${ROOT}/scripts/fetch_roots.sh"
bash "${ROOT}/scripts/verify_resources.sh"

echo "==> openssl armv7 (static libs)"
bash "${ROOT}/scripts/build_openssl_armv7.sh"

echo "==> openssl arm64"
bash "${ROOT}/scripts/build_openssl_arm64.sh"

if [[ ! -f "${MBED}/lib/libmbedtls-armv7.a" || \
      ! -f "${MBED}/lib/libmbedx509-armv7.a" || \
      ! -f "${MBED}/lib/libmbedcrypto-armv7.a" || \
      ! -f "${MBED}/lib/libmbedtls-arm64.a" || \
      ! -f "${MBED}/lib/libmbedx509-arm64.a" || \
      ! -f "${MBED}/lib/libmbedcrypto-arm64.a" ]]; then
  echo "==> bootstrap mbedtls"
  bash "${ROOT}/scripts/build_mbedtls.sh" "${MBED}"
fi

rm -rf "${SLICE}"
mkdir -p "${SLICE}/armv7" "${SLICE}/arm64"

echo "==> go backend core arm64 (iOS 12+)"
env SENKO_GO_CORE_SRC="${GO_CORE_SRC}" SENKO_GO="${GO_BIN}" \
  SENKO_TC="${TC}" SENKO_SDK_V64="${SDK_V64}" \
  bash "${ROOT}/scripts/build_go_core.sh" "${SLICE}/senko-core"

echo "==> daemon armv7"
make -C "${ROOT}/daemon" -f Makefile.ios clean \
  THEOS="${THEOS}" TC="${TC}" LDID="${TC}/ldid" SDK="${SDK_V7}" OSSL="${OSSL_V7}"
make -C "${ROOT}/daemon" -f Makefile.ios \
  THEOS="${THEOS}" TC="${TC}" LDID="${TC}/ldid" \
  TRIPLE=arm-apple-darwin11 SDK="${SDK_V7}" \
  ARCH="-arch armv7 -miphoneos-version-min=5.0" OSSL="${OSSL_V7}" \
  CRT="${CRT_V7}" \
  IOS_BINDIR=build/ios-armv7 all
cp "${ROOT}/daemon/build/ios-armv7/senkod" "${SLICE}/armv7/senkod"
cp "${ROOT}/daemon/build/ios-armv7/senkoctl" "${SLICE}/armv7/senkoctl"
cp "${ROOT}/daemon/build/ios-armv7/senko-kick" "${SLICE}/armv7/senko-kick"
cp "${ROOT}/daemon/build/ios-armv7/senkoawgd" "${SLICE}/armv7/senkoawgd"

echo "==> daemon arm64"
make -C "${ROOT}/daemon" -f Makefile.ios clean \
  THEOS="${THEOS}" TC="${TC}" LDID="${TC}/ldid" SDK="${SDK_V64}" OSSL="${OSSL_V64}"
make -C "${ROOT}/daemon" -f Makefile.ios \
  THEOS="${THEOS}" TC="${TC}" LDID="${TC}/ldid" \
  TRIPLE=arm64-apple-darwin SDK="${SDK_V64}" \
  ARCH="-arch arm64 -miphoneos-version-min=7.0" OSSL="${OSSL_V64}" \
  EXTRA_CFLAGS="${ARM64_ROOT_FLAGS}" \
  IOS_BINDIR=build/ios-arm64 all
cp "${ROOT}/daemon/build/ios-arm64/senkod" "${SLICE}/arm64/senkod"
cp "${ROOT}/daemon/build/ios-arm64/senkoctl" "${SLICE}/arm64/senkoctl"
cp "${ROOT}/daemon/build/ios-arm64/senko-kick" "${SLICE}/arm64/senko-kick"
cp "${ROOT}/daemon/build/ios-arm64/senkoawgd" "${SLICE}/arm64/senkoawgd"

make_fat "${SLICE}/senkod" "${SLICE}/armv7/senkod" "${SLICE}/arm64/senkod"
make_fat "${SLICE}/senkoctl" "${SLICE}/armv7/senkoctl" "${SLICE}/arm64/senkoctl"
make_fat "${SLICE}/senko-kick" "${SLICE}/armv7/senko-kick" "${SLICE}/arm64/senko-kick"
make_fat "${SLICE}/senkoawgd" "${SLICE}/armv7/senkoawgd" "${SLICE}/arm64/senkoawgd"
"${TC}/ldid" -S "${SLICE}/senkod" "${SLICE}/senkoctl" "${SLICE}/senko-kick" "${SLICE}/senkoawgd"

echo "==> app armv7"
make -C "${ROOT}/app" clean \
  THEOS="${THEOS}" TC="${TC}" LDID="${TC}/ldid" SDK="${SDK_V7}"
make -C "${ROOT}/app" \
  THEOS="${THEOS}" TC="${TC}" LDID="${TC}/ldid" \
  TRIPLE=arm-apple-darwin11 SDK="${SDK_V7}" \
  ARCH="-arch armv7 -miphoneos-version-min=5.0" \
  CRT="${CRT_V7}" \
  OBJDIR=build/obj-armv7 BIN=build/senko-armv7
cp "${ROOT}/app/build/senko-armv7" "${SLICE}/senko-armv7"

echo "==> app arm64"
make -C "${ROOT}/app" \
  THEOS="${THEOS}" TC="${TC}" LDID="${TC}/ldid" \
  TRIPLE=arm64-apple-darwin SDK="${SDK_V64}" \
  ARCH="-arch arm64 -miphoneos-version-min=7.0" \
  EXTRA_CFLAGS="${ARM64_ROOT_FLAGS}" \
  OBJDIR=build/obj-arm64 BIN=build/senko-arm64
cp "${ROOT}/app/build/senko-arm64" "${SLICE}/senko-arm64"

make_fat "${SLICE}/senko" "${SLICE}/senko-armv7" "${SLICE}/senko-arm64"
"${TC}/ldid" -S"${ROOT}/app/entitlements.plist" "${SLICE}/senko"

echo "==> senkotlsfix armv7"
make -C "${ROOT}/senkotlsfix" -f Makefile.ios clean \
  THEOS="${THEOS}" TC="${TC}" LDID="${TC}/ldid" SDK="${SDK_V7}" MBED="${MBED}"
make -C "${ROOT}/senkotlsfix" -f Makefile.ios \
  THEOS="${THEOS}" TC="${TC}" LDID="${TC}/ldid" \
  TRIPLE=arm-apple-darwin11 SDK="${SDK_V7}" \
  ARCH="-arch armv7 -miphoneos-version-min=5.0" \
  MBED="${MBED}" MBEDLIBS="${MBED}/lib/libmbedtls-armv7.a ${MBED}/lib/libmbedx509-armv7.a ${MBED}/lib/libmbedcrypto-armv7.a" \
  OUT=senkotlsfix-armv7.dylib
cp "${ROOT}/senkotlsfix/senkotlsfix-armv7.dylib" "${SLICE}/"

echo "==> senkotlsfix arm64"
make -C "${ROOT}/senkotlsfix" -f Makefile.ios \
  THEOS="${THEOS}" TC="${TC}" LDID="${TC}/ldid" \
  TRIPLE=arm64-apple-darwin SDK="${SDK_V64}" \
  ARCH="-arch arm64 -miphoneos-version-min=7.0" \
  EXTRA_CFLAGS="${ARM64_ROOT_FLAGS}" \
  INSTALL_NAME="${ARM64_TLSFIX_INSTALL_NAME}" \
  MBED="${MBED}" MBEDLIBS="${MBED}/lib/libmbedtls-arm64.a ${MBED}/lib/libmbedx509-arm64.a ${MBED}/lib/libmbedcrypto-arm64.a" \
  OUT=senkotlsfix-arm64.dylib
cp "${ROOT}/senkotlsfix/senkotlsfix-arm64.dylib" "${SLICE}/"

make_fat "${SLICE}/senkotlsfix.dylib" \
  "${SLICE}/senkotlsfix-armv7.dylib" "${SLICE}/senkotlsfix-arm64.dylib"
"${TC}/ldid" -S "${SLICE}/senkotlsfix.dylib"

echo "==> senkovpnicon armv7"
"${TC}/clang" -target arm-apple-darwin11 -B "${TC}" \
  -fno-objc-arc -Wall -Wextra -O2 -fPIC \
  -arch armv7 -miphoneos-version-min=5.0 -isysroot "${SDK_V7}" \
  "${ROOT}/vpnicon/senko_vpnicon.m" \
  -o "${SLICE}/senkovpnicon-armv7.dylib" \
  -dynamiclib \
  -Wl,-no_warn_inits \
  -install_name /usr/lib/senkovpnicon.dylib \
  -framework Foundation -framework UIKit -framework CoreFoundation -lobjc
"${TC}/ldid" -S "${SLICE}/senkovpnicon-armv7.dylib"

echo "==> senkovpnicon arm64"
"${TC}/clang" -target arm64-apple-darwin -B "${TC}" \
  -fno-objc-arc -Wall -Wextra -O2 -fPIC ${ARM64_ROOT_FLAGS} \
  -arch arm64 -miphoneos-version-min=7.0 -isysroot "${SDK_V64}" \
  "${ROOT}/vpnicon/senko_vpnicon.m" \
  -o "${SLICE}/senkovpnicon-arm64.dylib" \
  -dynamiclib \
  -Wl,-no_warn_inits \
  -install_name "${ARM64_VPNICON_INSTALL_NAME}" \
  -framework Foundation -framework UIKit -framework CoreFoundation -lobjc
"${TC}/ldid" -S "${SLICE}/senkovpnicon-arm64.dylib"

make_fat "${SLICE}/senkovpnicon.dylib" \
  "${SLICE}/senkovpnicon-armv7.dylib" "${SLICE}/senkovpnicon-arm64.dylib"
"${TC}/ldid" -S "${SLICE}/senkovpnicon.dylib"

echo "==> stage packaging"
rm -rf "${STAGE}"
mkdir -p "${STAGE}/DEBIAN" \
         "${PAYLOAD}/usr/bin" "${PAYLOAD}/usr/lib/senkotlsfix/roots" \
         "${PAYLOAD}/usr/lib/senko" \
         "${PAYLOAD}/usr/share/doc/senko" \
         "${PAYLOAD}/etc" \
         "${PAYLOAD}/Library/LaunchDaemons" \
         "${STAGE}/var/mobile/Library/Preferences" \
         "${PAYLOAD}/Applications/Senko.app"
cp "${ROOT}/packaging/DEBIAN/control" "${STAGE}/DEBIAN/control"
cp "${ROOT}/packaging/DEBIAN/postinst" "${STAGE}/DEBIAN/postinst"
cp "${ROOT}/packaging/DEBIAN/postrm" "${STAGE}/DEBIAN/postrm"
cp "${ROOT}/packaging/DEBIAN/prerm" "${STAGE}/DEBIAN/prerm"
cp "${ROOT}/packaging/var/mobile/Library/Preferences/com.senko.senkotlsfix.plist" \
   "${STAGE}/var/mobile/Library/Preferences/"
cp "${ROOT}/packaging/etc/pf.os" "${PAYLOAD}/etc/pf.os"
cp "${ROOT}/packaging/Library/LaunchDaemons/com.senko.senkod.plist" \
  "${PAYLOAD}/Library/LaunchDaemons/com.senko.senkod.plist"
cp "${SLICE}/senkotlsfix.dylib" "${PAYLOAD}/usr/lib/senkotlsfix.dylib"
cp "${SLICE}/senkovpnicon.dylib" "${PAYLOAD}/usr/lib/senkovpnicon.dylib"
cp "${ROOT}/vpnicon/senkovpnicon.plist" "${PAYLOAD}/usr/lib/senkovpnicon.plist"
cp "${ROOT}/senkotlsfix/substrate-filter.plist" \
   "${PAYLOAD}/usr/lib/senkotlsfix/substrate-filter.plist"
cp "${ROOT}/senkotlsfix/cacert.pem" "${PAYLOAD}/usr/lib/senkotlsfix/cacert.pem"
cp "${ROOT}/senkotlsfix/roots/"*.pem "${PAYLOAD}/usr/lib/senkotlsfix/roots/" 2>/dev/null || true
cp "${SLICE}/senkod" "${PAYLOAD}/usr/bin/senkod"
cp "${SLICE}/senkoctl" "${PAYLOAD}/usr/bin/senkoctl"
cp "${SLICE}/senko-kick" "${PAYLOAD}/usr/bin/senko-kick"
cp "${SLICE}/senkoawgd" "${PAYLOAD}/usr/bin/senkoawgd"
# kept under a private path because /usr/bin/head belongs to coreutils, and a
# package that claims it is refused outright by dpkg on any system that has it
if [ -f "${ROOT}/packaging/var/jb/usr/lib/senko/head" ]; then
  cp "${ROOT}/packaging/var/jb/usr/lib/senko/head" "${PAYLOAD}/usr/lib/senko/head"
fi
cp "${SLICE}/senko-core" "${PAYLOAD}/usr/lib/senko-core"
cp "${ROOT}/packaging/go-core-NOTICE" "${PAYLOAD}/usr/share/doc/senko/"
cp "${GO_CORE_SRC}/LICENSE" "${PAYLOAD}/usr/share/doc/senko/go-core-LICENSE"
cp "${SLICE}/senko" "${PAYLOAD}/Applications/Senko.app/senko"
cp "${ROOT}/app/Info.plist" "${PAYLOAD}/Applications/Senko.app/"
cp "${ROOT}/app/icons/"* "${PAYLOAD}/Applications/Senko.app/" 2>/dev/null || true
cp "${ROOT}/app/icons/flags/"*.png "${PAYLOAD}/Applications/Senko.app/" 2>/dev/null || true

# normalized metadata prevents legacy dpkg from rejecting builder ownership
chmod 755 "${PAYLOAD}/usr/bin/senkod" "${PAYLOAD}/usr/bin/senkoctl" "${PAYLOAD}/usr/bin/senkoawgd" \
          "${PAYLOAD}/usr/lib/senko-core" \
          "${PAYLOAD}/Applications/Senko.app" \
          "${PAYLOAD}/Applications/Senko.app/senko" \
          "${PAYLOAD}/usr/lib/senkotlsfix.dylib" \
          "${PAYLOAD}/usr/lib/senkovpnicon.dylib"
if [ -f "${PAYLOAD}/usr/lib/senko/head" ]; then
  chmod 755 "${PAYLOAD}/usr/lib/senko/head"
fi
# setuid is required because the sandboxed app cannot signal the root daemon
chown 0:0 "${PAYLOAD}/usr/bin/senko-kick" 2>/dev/null || true
chmod 4755 "${PAYLOAD}/usr/bin/senko-kick"
find "${PAYLOAD}/Applications/Senko.app" -type f ! -name senko -exec chmod 644 {} +
find "${PAYLOAD}/usr/lib/senkotlsfix" -type f -exec chmod 644 {} +
chmod 644 "${PAYLOAD}/usr/lib/senkovpnicon.plist"
chmod 644 "${PAYLOAD}/usr/share/doc/senko/"*
chmod 644 "${PAYLOAD}/etc/pf.os"
chmod 644 "${PAYLOAD}/Library/LaunchDaemons/"*.plist
chmod 644 "${STAGE}/var/mobile/Library/Preferences/"*.plist 2>/dev/null || true
chmod 755 "${STAGE}/DEBIAN/postinst" "${STAGE}/DEBIAN/postrm" "${STAGE}/DEBIAN/prerm"
chmod 644 "${STAGE}/DEBIAN/control"

echo "==> verify dual arch"
for bin in "${PAYLOAD}/usr/bin/senkod" \
           "${PAYLOAD}/usr/bin/senkoctl" \
           "${PAYLOAD}/usr/bin/senko-kick" \
           "${PAYLOAD}/usr/bin/senkoawgd" \
           "${PAYLOAD}/Applications/Senko.app/senko" \
           "${PAYLOAD}/usr/lib/senkotlsfix.dylib"; do
  info="$(file "${bin}")"
  echo "${info}"
  case "${info}" in
    *armv7*arm64*|*arm64*armv7*) ;;
    *) echo "missing armv7+arm64 slices in ${bin}" >&2; exit 1 ;;
  esac
  "${LIPO}" -info "${bin}"
done

echo "==> verify vpn icon hook"
file "${PAYLOAD}/usr/lib/senkovpnicon.dylib"
"${LIPO}" -info "${PAYLOAD}/usr/lib/senkovpnicon.dylib"

echo "==> verify go backend core"
file "${PAYLOAD}/usr/lib/senko-core" | grep -q 'Mach-O 64-bit arm64 executable'
"${TC}/otool" -l "${PAYLOAD}/usr/lib/senko-core" \
  | grep -A5 LC_BUILD_VERSION | grep -q 'minos 12.0'

echo "==> generate md5sums (data files only, no ./ prefix)"
# checksums run after chmod so they describe the final payload bytes
(
  cd "${STAGE}"
  : > DEBIAN/md5sums
  find . -type f ! -path './DEBIAN/*' -printf '%P\0' \
    | sort -z \
    | while IFS= read -r -d '' rel; do
        md5sum "${rel}" >> DEBIAN/md5sums
      done
)
if grep -qE ' \./|^$|/$' "${STAGE}/DEBIAN/md5sums"; then
  echo "bad md5sums paths" >&2
  exit 1
fi
nfiles="$(find "${STAGE}" -type f ! -path "${STAGE}/DEBIAN/*" | wc -l)"
nsums="$(grep -c . "${STAGE}/DEBIAN/md5sums" || true)"
if [ "${nfiles}" -ne "${nsums}" ]; then
  echo "md5sums count ${nsums} != data files ${nfiles}" >&2
  exit 1
fi
(cd "${STAGE}" && md5sum -c DEBIAN/md5sums) >/dev/null
echo "md5sums ok (${nsums} files)"

echo "==> pack deb (cydia-safe tar/ar)"
# GNU headers remain readable by the oldest supported device dpkg
# fixed mtimes make identical payloads reproducible
cd "${ROOT}"
rm -f debian-binary control.tar.gz data.tar.gz "${OUT}"
printf '2.0\n' > debian-binary

pack_tar() {
  local out="$1"; shift
  tar --format=gnu \
      --owner=0 --group=0 --numeric-owner \
      --mtime='2020-01-01 00:00:00' \
      --sort=name \
      -czf "${out}" "$@"
}

(
  cd "${STAGE}/DEBIAN"
  pack_tar ../../control.tar.gz control md5sums postinst postrm prerm
)
(
  cd "${STAGE}"
  pack_tar ../data.tar.gz --exclude=./DEBIAN --exclude=./DEBIAN/* .
)

# dpkg requires debian-binary before the control and data members
rm -f "${OUT}"
if "${HOST_AR}" -rcD "${OUT}" debian-binary control.tar.gz data.tar.gz 2>/dev/null; then
  :
else
  "${HOST_AR}" -rc "${OUT}" debian-binary control.tar.gz data.tar.gz
fi
rm -f debian-binary control.tar.gz data.tar.gz

echo "==> verify packed deb"
chmod +x "${ROOT}/scripts/verify_deb.sh"
bash "${ROOT}/scripts/verify_deb.sh" "${OUT}"
bash "${ROOT}/scripts/verify_runtime_deps.sh" "${OUT}"

SIZE="$(stat -c%s "${OUT}" 2>/dev/null || stat -f%z "${OUT}")"
MD5="$(md5sum "${OUT}" | awk '{print $1}')"
SHA1="$(sha1sum "${OUT}" | awk '{print $1}')"
SHA256="$(sha256sum "${OUT}" | awk '{print $1}')"
FILENAME="$(basename "${OUT}")"

{
  cat "${STAGE}/DEBIAN/control"
  echo "Filename: debs/${FILENAME}"
  echo "Size: ${SIZE}"
  echo "MD5sum: ${MD5}"
  echo "SHA1: ${SHA1}"
  echo "SHA256: ${SHA256}"
} > "${ROOT}/${FILENAME%.deb}.Packages"

ls -lh "${OUT}"
echo "done: ${OUT}"
echo "packages stanza: ${ROOT}/${FILENAME%.deb}.Packages"
echo ""
echo "==> Cydia Packages stanza (repo hashes must match this exact file)"
cat "${ROOT}/${FILENAME%.deb}.Packages"
echo "----"
echo "hash sum mismatch almost always means Packages/Packages.bz2/Release"
echo "still describe an older deb. re-index the repo after every upload."
