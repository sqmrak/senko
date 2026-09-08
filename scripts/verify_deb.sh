#!/usr/bin/env bash
# legacy dpkg needs deterministic ownership, member order, and checksum paths
set -euo pipefail

DEB="${1:-}"
HOST_AR="${SENKO_HOST_AR:-/usr/bin/ar}"
if [ -z "${DEB}" ] || [ ! -f "${DEB}" ]; then
  echo "usage: $0 package.deb" >&2
  exit 2
fi

WORK="$(mktemp -d)"
trap 'rm -rf "${WORK}"' EXIT

cp "${DEB}" "${WORK}/pkg.deb"
cd "${WORK}"
"${HOST_AR}" x pkg.deb

for m in debian-binary control.tar.gz data.tar.gz; do
  [ -f "${m}" ] || { echo "missing ar member ${m}" >&2; exit 1; }
done

# dpkg expects the three archive members in this order
order="$("${HOST_AR}" t pkg.deb | tr '\n' ' ')"
case "${order}" in
  "debian-binary control.tar.gz data.tar.gz "*) ;;
  *)
    echo "bad ar member order: ${order}" >&2
    exit 1
    ;;
esac

echo "2.0" | cmp -s - debian-binary || {
  echo "debian-binary must be '2.0'" >&2
  exit 1
}

mkdir control data
tar -xzf control.tar.gz -C control
tar -xzf data.tar.gz -C data

for f in control md5sums postinst postrm prerm; do
  [ -f "control/${f}" ] || { echo "control.tar missing ${f}" >&2; exit 1; }
done
[ -x control/postinst ] || { echo "postinst not executable in tar" >&2; exit 1; }

arch="$(awk -F': ' '$1 == "Architecture" { print $2; exit }' control/control)"
if [ "${arch}" != all ]; then
  echo "universal package Architecture must be all, got ${arch:-<empty>}" >&2
  exit 1
fi

[ -x data/var/jb/Applications/Senko.app/senko ] || {
  echo "universal app payload missing" >&2; exit 1;
}
[ -x data/var/jb/usr/bin/senkod ] || {
  echo "universal daemon payload missing" >&2; exit 1;
}
[ -f data/var/jb/Library/LaunchDaemons/com.senko.senkod.plist ] || {
  echo "universal launchd plist missing" >&2; exit 1;
}
[ -f data/var/jb/etc/pf.os ] || {
  echo "universal pf.os compatibility file missing" >&2; exit 1;
}
if [ -e data/Applications ] || [ -e data/usr ] || [ -e data/Library ]; then
  echo "universal package would write into the rootless sealed root" >&2
  exit 1
fi
grep -q '<string>/var/jb/usr/bin/senkod</string>' \
  data/var/jb/Library/LaunchDaemons/com.senko.senkod.plist || {
    echo "universal launchd program path mismatch" >&2; exit 1;
  }
grep -q 'install_rootful_layout' control/postinst || {
  echo "rootful compatibility installer missing" >&2; exit 1;
}

if grep -R -q '@SENKO_' control data; then
  echo "unexpanded packaging token" >&2
  exit 1
fi

# builder ownership makes legacy dpkg reject otherwise valid payloads
bad_uid="$(tar -tvzf data.tar.gz | awk 'NR>0 && $2 !~ /root|0\// {print; exit 1}')" || true
# tar listing formats disagree across hosts, while tarfile exposes numeric ids
python3 - <<'PY'
import tarfile, sys
for name in ("data.tar.gz", "control.tar.gz"):
    t = tarfile.open(name)
    for m in t.getmembers():
        if m.uid != 0 or m.gid != 0:
            print(f"{name}: {m.name} uid={m.uid} gid={m.gid} (want 0/0)", file=sys.stderr)
            sys.exit(1)
print("ownership ok (uid/gid 0)")
PY

# exact coverage detects stale files carried from an earlier package stage
(
  cd data
  # md5sums paths are relative without the leading ./ on legacy dpkg
  mapfile -t files < <(find . -type f | sed 's#^\./##' | sort)
  mapfile -t sums < <(awk '{print $2}' ../control/md5sums | sort)
  if [ "${#files[@]}" -ne "${#sums[@]}" ]; then
    echo "file count ${#files[@]} != md5sums ${#sums[@]}" >&2
    exit 1
  fi
  md5sum -c ../control/md5sums >/dev/null
)
echo "md5sums ok"

# legacy dpkg resolves checksum entries without a leading ./
if grep -qE ' \./' control/md5sums; then
  echo "md5sums contains ./ paths" >&2
  exit 1
fi

echo "verify_deb: ok ${DEB}"
