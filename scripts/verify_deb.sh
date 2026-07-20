#!/usr/bin/env bash
# verify legacy dpkg layout, ownership, modes, and payload checksums
set -euo pipefail

DEB="${1:-}"
if [ -z "${DEB}" ] || [ ! -f "${DEB}" ]; then
  echo "usage: $0 package.deb" >&2
  exit 2
fi

WORK="$(mktemp -d)"
trap 'rm -rf "${WORK}"' EXIT

cp "${DEB}" "${WORK}/pkg.deb"
cd "${WORK}"
ar x pkg.deb

for m in debian-binary control.tar.gz data.tar.gz; do
  [ -f "${m}" ] || { echo "missing ar member ${m}" >&2; exit 1; }
done

# dpkg expects the three archive members in this order
order="$(ar t pkg.deb | tr '\n' ' ')"
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

# reject builder ownership because legacy dpkg expects root-owned files
bad_uid="$(tar -tvzf data.tar.gz | awk 'NR>0 && $2 !~ /root|0\// {print; exit 1}')" || true
# use python because tar metadata parsing is not portable in shell
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

# require one matching checksum entry for every payload file
(
  cd data
  # normalize find paths to the checksum format
  mapfile -t files < <(find . -type f | sed 's#^\./##' | sort)
  mapfile -t sums < <(awk '{print $2}' ../control/md5sums | sort)
  if [ "${#files[@]}" -ne "${#sums[@]}" ]; then
    echo "file count ${#files[@]} != md5sums ${#sums[@]}" >&2
    exit 1
  fi
  md5sum -c ../control/md5sums >/dev/null
)
echo "md5sums ok"

# reject checksum paths that legacy dpkg cannot resolve
if grep -qE ' \./' control/md5sums; then
  echo "md5sums contains ./ paths" >&2
  exit 1
fi

echo "verify_deb: ok ${DEB}"
