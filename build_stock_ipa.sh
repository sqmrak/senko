#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")" && pwd)"
THEOS="${THEOS:?set THEOS to theos root}"
TC="${SENKO_TC:-${THEOS}/toolchain/linux/iphone/bin}"
SDK="${SENKO_SDK_V64:?set SENKO_SDK_V64 to the arm64 iPhoneOS SDK}"
OSSL="${SENKO_OSSL_V64:?set SENKO_OSSL_V64 to the arm64 OpenSSL prefix}"
GO="${SENKO_GO:?set SENKO_GO to Go 1.27.1}"
GO_CORE_SRC="${SENKO_GO_CORE_SRC:?set SENKO_GO_CORE_SRC to the pinned Go core source}"
LDID="${TC}/ldid"
PKG_VERSION="$(awk -F': ' '$1 == "Version" { print $2; exit }' "${ROOT}/packaging/DEBIAN/control")"
BUILD="${ROOT}/.ipa-build"
APP_STAGE="${BUILD}/Payload/Senko.app"
IPA="${ROOT}/Senko-v${PKG_VERSION}.ipa"

rm -rf "${BUILD}" "${IPA}"
mkdir -p "${APP_STAGE}"

make -C "${ROOT}/app" clean \
  THEOS="${THEOS}" TC="${TC}" SDK="${SDK}" LDID="${LDID}"
make -C "${ROOT}/app" \
  THEOS="${THEOS}" TC="${TC}" SDK="${SDK}" LDID="${LDID}" \
  TRIPLE=arm64-apple-darwin ARCH="-arch arm64 -miphoneos-version-min=9.0" \
  STOCK_NATIVE=1 OSSL="${OSSL}" ENTITLEMENTS="${ROOT}/app/stock_entitlements.plist" \
  OBJDIR=build/obj-stock-arm64 BIN=build/senko-stock-arm64

cp "${ROOT}/app/build/senko-stock-arm64" "${APP_STAGE}/senko"
"${LDID}" -S"${ROOT}/app/stock_entitlements.plist" "${APP_STAGE}/senko"
awk '
  !done && $0 == "    <key>MinimumOSVersion</key>" {
    print
    getline
    sub("<string>5.0</string>", "<string>9.0</string>")
    done = 1
  }
  { print }
' "${ROOT}/app/Info.plist" > "${APP_STAGE}/Info.plist"
cp "${ROOT}/app/icons/"*.png "${APP_STAGE}/"
cp "${ROOT}/app/icons/"*.jpg "${APP_STAGE}/" 2>/dev/null || true
cp "${ROOT}/app/icons/"*.caf "${APP_STAGE}/" 2>/dev/null || true
cp "${ROOT}/app/icons/"*.wav "${APP_STAGE}/" 2>/dev/null || true
cp -R "${ROOT}/app/icons/flags" "${APP_STAGE}/"

make -C "${ROOT}/native" clean all \
  THEOS="${THEOS}" TC="${TC}" LDID="${LDID}" SDK="${SDK}" \
  GO="${GO}" GO_CORE_SRC="${GO_CORE_SRC}" \
  ARCH="-arch arm64 -miphoneos-version-min=9.0" BUILD="${BUILD}/native"
mkdir -p "${APP_STAGE}/PlugIns"
cp -R "${BUILD}/native/SenkoTunnel.appex" "${APP_STAGE}/PlugIns/"

chmod 755 "${APP_STAGE}/senko" \
  "${APP_STAGE}/PlugIns/SenkoTunnel.appex/SenkoTunnel"
(cd "${BUILD}" && zip -qr "${IPA}" Payload)
bash "${ROOT}/scripts/verify_ipa.sh" "${IPA}"
echo "built ${IPA}"
