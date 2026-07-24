#!/usr/bin/env bash
# reject payloads that require non-system device libraries
set -euo pipefail

DEB="${1:-}"
if [ -z "${DEB}" ] || [ ! -f "${DEB}" ]; then
  echo "usage: $0 package.deb" >&2
  exit 2
fi

THEOS="${THEOS:?set THEOS to theos root}"
TC="${SENKO_TC:-${THEOS}/toolchain/linux/iphone/bin}"
WORK="$(mktemp -d)"
trap 'rm -rf "${WORK}"' EXIT

cp "${DEB}" "${WORK}/pkg.deb"
(
  cd "${WORK}"
  ar x pkg.deb control.tar.gz data.tar.gz
  mkdir control data
  tar -xzf control.tar.gz -C control
  tar -xzf data.tar.gz -C data

  deps="$(awk -F': ' '$1 == "Depends" { print $2 }' control/control)"
  case "${deps}" in
    "firmware (>= 5.0)"|"firmware (>= 5.0), mobilesubstrate") ;;
    *)
      echo "unexpected Depends: ${deps:-<none>}" >&2
      exit 1
      ;;
  esac
  if grep -Eq '^(Pre-Depends|Recommends|Suggests):' control/control; then
    echo "package declares optional or external package dependencies" >&2
    exit 1
  fi

  [ -x data/usr/bin/senkod ] || { echo "missing senkod" >&2; exit 1; }
  [ -x data/usr/bin/senkoawgd ] || { echo "missing senkoawgd" >&2; exit 1; }
  awg_bins="$(find data/usr/bin -maxdepth 1 -type f -name '*awgd' -print)"
  [ "${awg_bins}" = "data/usr/bin/senkoawgd" ] || {
    echo "stale awg daemon binary" >&2
    exit 1
  }
  [ ! -e data/usr/bin/redsocks-senko ] || {
    echo "redsocks must not be packaged" >&2
    exit 1
  }
  [ -f data/usr/lib/senkotlsfix.dylib ] || { echo "missing bundled tlsfix" >&2; exit 1; }
  [ ! -e data/Library/MobileSubstrate ] || {
    echo "MobileSubstrate payload must be installed only when available" >&2
    exit 1
  }

  for bin in data/usr/bin/senkod data/usr/bin/senkoctl data/usr/bin/senkoawgd \
             data/usr/lib/senkotlsfix.dylib \
             data/usr/lib/senkovpnicon.dylib \
             data/Applications/Senko.app/senko; do
    while IFS= read -r dep; do
      case "${dep}" in
        /usr/lib/*|/System/Library/Frameworks/*) ;;
        *) echo "non-system runtime dependency in ${bin}: ${dep}" >&2; exit 1 ;;
      esac
    done < <("${TC}/otool" -L "${bin}" | awk '/^[[:space:]]/ { print $1 }')
  done
)

echo "runtime dependencies: self-contained"
