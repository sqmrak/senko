#!/usr/bin/env bash
# build a reproducible fat package for both runtime architectures
set -euo pipefail

ROOT="$(cd "$(dirname "$0")" && pwd)"
# override with THEOS / SENKO_* env vars; defaults are portable under $HOME
THEOS="${THEOS:-${HOME}/theos}"
TC="${SENKO_TC:-${THEOS}/toolchain/linux/iphone/bin}"
LIPO="${TC}/lipo"
OUT="${ROOT}/senko-v1.0.2-stable.deb"
# thin armv7 sdk: fat dylib remap is flaky on linux aarch64 hosts
SDK_V7="${SENKO_SDK_V7:-${HOME}/sdks-armv7}"
SDK_V64="${SENKO_SDK_V64:-${HOME}/sdks/iPhoneOS11.4.sdk}"
OSSL_V7="${SENKO_OSSL_V7:-${HOME}/src/openssl-build/install-arm-o1}"
OSSL_V64="${SENKO_OSSL_V64:-${HOME}/src/openssl-build/install-arm64-o1}"
# static mbedtls for the tlsfix hook (no device-side dylib)
MBED="${SENKO_MBED:-${HOME}/.cache/senko/mbedtls-ios-3.6.4}"
STAGE="${ROOT}/packaging"
SLICE="${ROOT}/.build-slices"

make_fat() {
  local out="$1"
  shift
  "${LIPO}" -create "$@" -output "${out}"
}

echo "==> host tests"
# keep system compiler first so host tests are not built with the ios clang
make -C "${ROOT}/tests" test CC="${CC:-/usr/bin/cc}"

echo "==> fetch tls roots"
bash "${ROOT}/scripts/fetch_roots.sh"
bash "${ROOT}/scripts/verify_resources.sh"

echo "==> openssl armv7 (static libs)"
if [[ ! -f "${OSSL_V7}/lib/libssl.a" || ! -f "${OSSL_V7}/lib/libcrypto.a" ]]; then
  echo "missing ${OSSL_V7}/lib - build armv7 openssl first" >&2
  exit 1
fi

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

echo "==> daemon armv7"
make -C "${ROOT}/daemon" -f Makefile.ios clean
make -C "${ROOT}/daemon" -f Makefile.ios \
  TRIPLE=arm-apple-darwin11 SDK="${SDK_V7}" \
  ARCH="-arch armv7 -miphoneos-version-min=5.0" OSSL="${OSSL_V7}" \
  IOS_BINDIR=build/ios-armv7 all
cp "${ROOT}/daemon/build/ios-armv7/senkod" "${SLICE}/armv7/senkod"
cp "${ROOT}/daemon/build/ios-armv7/senkoctl" "${SLICE}/armv7/senkoctl"
cp "${ROOT}/daemon/build/ios-armv7/senko-kick" "${SLICE}/armv7/senko-kick"
cp "${ROOT}/daemon/build/ios-armv7/senkoawgd" "${SLICE}/armv7/senkoawgd"

echo "==> daemon arm64"
make -C "${ROOT}/daemon" -f Makefile.ios clean
make -C "${ROOT}/daemon" -f Makefile.ios \
  TRIPLE=arm64-apple-darwin SDK="${SDK_V64}" \
  ARCH="-arch arm64 -miphoneos-version-min=7.0" OSSL="${OSSL_V64}" \
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
make -C "${ROOT}/app" clean
make -C "${ROOT}/app" \
  TRIPLE=arm-apple-darwin11 SDK="${SDK_V7}" \
  ARCH="-arch armv7 -miphoneos-version-min=5.0" \
  OBJDIR=build/obj-armv7 BIN=build/senko-armv7
cp "${ROOT}/app/build/senko-armv7" "${SLICE}/senko-armv7"

echo "==> app arm64"
make -C "${ROOT}/app" \
  TRIPLE=arm64-apple-darwin SDK="${SDK_V64}" \
  ARCH="-arch arm64 -miphoneos-version-min=7.0" \
  OBJDIR=build/obj-arm64 BIN=build/senko-arm64
cp "${ROOT}/app/build/senko-arm64" "${SLICE}/senko-arm64"

make_fat "${SLICE}/senko" "${SLICE}/senko-armv7" "${SLICE}/senko-arm64"
"${TC}/ldid" -S"${ROOT}/app/entitlements.plist" "${SLICE}/senko"

echo "==> senkotlsfix armv7"
make -C "${ROOT}/senkotlsfix" -f Makefile.ios clean
make -C "${ROOT}/senkotlsfix" -f Makefile.ios \
  TRIPLE=arm-apple-darwin11 SDK="${SDK_V7}" \
  ARCH="-arch armv7 -miphoneos-version-min=5.0" \
  MBED="${MBED}" MBEDLIBS="${MBED}/lib/libmbedtls-armv7.a ${MBED}/lib/libmbedx509-armv7.a ${MBED}/lib/libmbedcrypto-armv7.a" \
  OUT=senkotlsfix-armv7.dylib
cp "${ROOT}/senkotlsfix/senkotlsfix-armv7.dylib" "${SLICE}/"

echo "==> senkotlsfix arm64"
make -C "${ROOT}/senkotlsfix" -f Makefile.ios \
  TRIPLE=arm64-apple-darwin SDK="${SDK_V64}" \
  ARCH="-arch arm64 -miphoneos-version-min=7.0" \
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
  -fno-objc-arc -Wall -Wextra -O2 -fPIC \
  -arch arm64 -miphoneos-version-min=7.0 -isysroot "${SDK_V64}" \
  "${ROOT}/vpnicon/senko_vpnicon.m" \
  -o "${SLICE}/senkovpnicon-arm64.dylib" \
  -dynamiclib \
  -Wl,-no_warn_inits \
  -install_name /usr/lib/senkovpnicon.dylib \
  -framework Foundation -framework UIKit -framework CoreFoundation -lobjc
"${TC}/ldid" -S "${SLICE}/senkovpnicon-arm64.dylib"

make_fat "${SLICE}/senkovpnicon.dylib" \
  "${SLICE}/senkovpnicon-armv7.dylib" "${SLICE}/senkovpnicon-arm64.dylib"
"${TC}/ldid" -S "${SLICE}/senkovpnicon.dylib"

echo "==> stage packaging"
rm -rf "${STAGE}/usr/bin"
mkdir -p "${STAGE}/usr/bin" "${STAGE}/usr/lib/senkotlsfix/roots" \
         "${STAGE}/var/mobile/Library/Preferences" \
         "${STAGE}/Applications"
rm -rf "${STAGE}/Library/MobileSubstrate"
cp "${SLICE}/senkotlsfix.dylib" "${STAGE}/usr/lib/senkotlsfix.dylib"
cp "${SLICE}/senkovpnicon.dylib" "${STAGE}/usr/lib/senkovpnicon.dylib"
cp "${ROOT}/vpnicon/senkovpnicon.plist" "${STAGE}/usr/lib/senkovpnicon.plist"
cp "${ROOT}/senkotlsfix/substrate-filter.plist" \
   "${STAGE}/usr/lib/senkotlsfix/substrate-filter.plist"
cp "${ROOT}/senkotlsfix/cacert.pem" "${STAGE}/usr/lib/senkotlsfix/cacert.pem"
cp "${ROOT}/senkotlsfix/roots/"*.pem "${STAGE}/usr/lib/senkotlsfix/roots/" 2>/dev/null || true
cp "${SLICE}/senkod" "${STAGE}/usr/bin/senkod"
cp "${SLICE}/senkoctl" "${STAGE}/usr/bin/senkoctl"
cp "${SLICE}/senko-kick" "${STAGE}/usr/bin/senko-kick"
cp "${SLICE}/senkoawgd" "${STAGE}/usr/bin/senkoawgd"
rm -f "${STAGE}/usr/bin/redsocks-senko"
rm -rf "${STAGE}/Applications/Senko.app"
mkdir -p "${STAGE}/Applications/Senko.app"
cp "${SLICE}/senko" "${STAGE}/Applications/Senko.app/senko"
cp "${ROOT}/app/Info.plist" "${STAGE}/Applications/Senko.app/"
cp "${ROOT}/app/icons/"* "${STAGE}/Applications/Senko.app/" 2>/dev/null || true
cp "${ROOT}/app/icons/flags/"*.png "${STAGE}/Applications/Senko.app/" 2>/dev/null || true

# normalize ownership and modes because legacy dpkg rejects builder metadata
chmod 755 "${STAGE}/usr/bin/senkod" "${STAGE}/usr/bin/senkoctl" "${STAGE}/usr/bin/senkoawgd" \
          "${STAGE}/Applications/Senko.app" \
          "${STAGE}/Applications/Senko.app/senko" \
          "${STAGE}/usr/lib/senkotlsfix.dylib" \
  "${STAGE}/usr/lib/senkovpnicon.dylib" 2>/dev/null || true
# mark the helper setuid so the mobile ui can restart senkod
chown 0:0 "${STAGE}/usr/bin/senko-kick" 2>/dev/null || true
chmod 4755 "${STAGE}/usr/bin/senko-kick"
find "${STAGE}/Applications/Senko.app" -type f ! -name senko -exec chmod 644 {} +
find "${STAGE}/usr/lib/senkotlsfix" -type f -exec chmod 644 {} +
chmod 644 "${STAGE}/usr/lib/senkovpnicon.plist" 2>/dev/null || true
chmod 644 "${STAGE}/Library/LaunchDaemons/"*.plist 2>/dev/null || true
chmod 644 "${STAGE}/var/mobile/Library/Preferences/"*.plist 2>/dev/null || true
chmod 755 "${STAGE}/DEBIAN/postinst" "${STAGE}/DEBIAN/postrm" "${STAGE}/DEBIAN/prerm"
chmod 644 "${STAGE}/DEBIAN/control"

echo "==> verify dual arch"
for bin in "${STAGE}/usr/bin/senkod" \
           "${STAGE}/usr/bin/senkoctl" \
           "${STAGE}/usr/bin/senko-kick" \
           "${STAGE}/usr/bin/senkoawgd" \
           "${STAGE}/Applications/Senko.app/senko" \
           "${STAGE}/usr/lib/senkotlsfix.dylib"; do
  info="$(file "${bin}")"
  echo "${info}"
  case "${info}" in
    *armv7*arm64*|*arm64*armv7*) ;;
    *) echo "missing armv7+arm64 slices in ${bin}" >&2; exit 1 ;;
  esac
  "${LIPO}" -info "${bin}"
done

echo "==> verify vpn icon hook"
file "${STAGE}/usr/lib/senkovpnicon.dylib"
"${LIPO}" -info "${STAGE}/usr/lib/senkovpnicon.dylib"

echo "==> generate md5sums (data files only, no ./ prefix)"
# regenerate checksums after modes change and keep paths dpkg-compatible
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
# use legacy tar metadata so iphoneos dpkg reads ownership and headers correctly
# fixed mtimes keep identical payloads reproducible
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
  cd packaging/DEBIAN
  pack_tar ../../control.tar.gz control md5sums postinst postrm prerm
)
(
  cd packaging
  pack_tar ../data.tar.gz --exclude=./DEBIAN --exclude=./DEBIAN/* .
)

# keep the archive member order required by dpkg
rm -f "${OUT}"
if ar -rcD "${OUT}" debian-binary control.tar.gz data.tar.gz 2>/dev/null; then
  :
else
  ar -rc "${OUT}" debian-binary control.tar.gz data.tar.gz
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

# store release metadata beside the package for repository tooling
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
