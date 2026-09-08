#!/usr/bin/env bash
# undeclared device libraries turn installation success into a launch failure
set -euo pipefail

HOST_AR="${SENKO_HOST_AR:-/usr/bin/ar}"

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
  "${HOST_AR}" x pkg.deb control.tar.gz data.tar.gz
  mkdir control data
  tar -xzf control.tar.gz -C control
  tar -xzf data.tar.gz -C data

  arch="$(awk -F': ' '$1 == "Architecture" { print $2; exit }' control/control)"
  case "${arch}" in
    all) relroot="var/jb" ;;
    *) echo "universal package architecture must be all, got ${arch:-<none>}" >&2; exit 1 ;;
  esac
  payload="data${relroot:+/${relroot}}"

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

  [ -x "${payload}/usr/bin/senkod" ] || { echo "missing senkod" >&2; exit 1; }
  [ -x "${payload}/usr/bin/senkoawgd" ] || { echo "missing senkoawgd" >&2; exit 1; }
  awg_bins="$(find "${payload}/usr/bin" -maxdepth 1 -type f -name '*awgd' -print)"
  [ "${awg_bins}" = "${payload}/usr/bin/senkoawgd" ] || {
    echo "stale awg daemon binary" >&2
    exit 1
  }
  [ ! -e "${payload}/usr/bin/redsocks-senko" ] || {
    echo "redsocks must not be packaged" >&2
    exit 1
  }
  [ -f "${payload}/usr/lib/senkotlsfix.dylib" ] || { echo "missing bundled tlsfix" >&2; exit 1; }
  [ ! -e "${payload}/Library/MobileSubstrate" ] || {
    echo "MobileSubstrate payload must be installed only when available" >&2
    exit 1
  }

  for bin in "${payload}/usr/bin/senkod" "${payload}/usr/bin/senkoctl" "${payload}/usr/bin/senkoawgd" \
             "${payload}/usr/lib/senkotlsfix.dylib" \
             "${payload}/usr/lib/senkovpnicon.dylib" \
             "${payload}/Applications/Senko.app/senko"; do
    while IFS= read -r dep; do
      case "${dep}" in
        /usr/lib/*|/System/Library/Frameworks/*|/var/jb/usr/lib/senkotlsfix.dylib|/var/jb/usr/lib/senkovpnicon.dylib) ;;
        *) echo "non-system runtime dependency in ${bin}: ${dep}" >&2; exit 1 ;;
      esac
    done < <("${TC}/otool" -L "${bin}" | awk '/^[[:space:]]/ { print $1 }')
  done
)

echo "runtime dependencies: self-contained"
