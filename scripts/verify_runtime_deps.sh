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
    "firmware (>= 4.3)"|"firmware (>= 4.3), mobilesubstrate") ;;
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
  # amneziawg runs inside senkod
  [ -z "$(find "${payload}/usr/bin" -maxdepth 1 -name '*awgd' -print)" ] || {
    echo "a separate amneziawg daemon must not be packaged" >&2
    exit 1
  }
  [ ! -e "${payload}/usr/bin/redsocks-senko" ] || {
    echo "redsocks must not be packaged" >&2
    exit 1
  }
  [ -f "${payload}/usr/lib/senkotlsfix.dylib" ] || { echo "missing bundled tlsfix" >&2; exit 1; }
  # senkod runs root work for the app itself; some jailbreaks (h3lix,
  # doubleh3lix, sockport) never honour a setuid bit for an app's child, so
  # the package must not rely on one. read the modes tar stored
  setuid_entries="$(tar -tvf data.tar.gz | awk '$1 ~ /^-..[sS]/')"
  [ -z "${setuid_entries}" ] || {
    echo "setuid files must not be packaged: ${setuid_entries}" >&2
    exit 1
  }
  [ ! -e "${payload}/usr/bin/senko-kick" ] || {
    echo "senko-kick must not be packaged" >&2
    exit 1
  }
  [ ! -e "${payload}/Library/MobileSubstrate" ] || {
    echo "MobileSubstrate payload must be installed only when available" >&2
    exit 1
  }

  for bin in "${payload}/usr/bin/senkod" "${payload}/usr/bin/senkoctl" \
             "${payload}/Applications/Senko.app/senko" \
             "${payload}/usr/lib/senkotlsfix.dylib" \
             "${payload}/usr/lib/senkostatus.dylib"; do
    while IFS= read -r dep; do
      case "${dep}" in
        /usr/lib/*|/System/Library/Frameworks/*|/var/jb/usr/lib/senkotlsfix.dylib|/var/jb/usr/lib/senkostatus.dylib) ;;
        *) echo "non-system runtime dependency in ${bin}: ${dep}" >&2; exit 1 ;;
      esac
    done < <("${TC}/otool" -L "${bin}" | awk '/^[[:space:]]/ { print $1 }')
    slices="$("${TC}/lipo" -archs "${bin}")"
    case "${bin}" in
      *.dylib) expected="armv7 arm64 arm64e" ;;
      *) expected="armv7 arm64" ;;
    esac
    if [ "${slices}" != "${expected}" ]; then
      echo "unexpected slices in ${bin}: ${slices} (expected ${expected})" >&2
      exit 1
    fi
  done
)

echo "runtime dependencies: self-contained"
