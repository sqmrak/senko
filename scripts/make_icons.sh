#!/usr/bin/env bash
# cut the home screen icon to every size springboard actually asks for
#
# a bundle that ships one size makes springboard scale it into whichever slot
# the device uses, and the pressed state is still drawn at the slot's own rect:
# the icon then sits a little outside its own highlight. one file per slot is
# the whole fix.
#
# the master defaults to the largest icon in the tree. point SENKO_ICON_MASTER
# at real artwork (1024px) to get sharp 3x and ipad sizes.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
ICONS="${ROOT}/app/icons"
MASTER="${SENKO_ICON_MASTER:-${ICONS}/Icon@2x.png}"

[ -f "${MASTER}" ] || { echo "no icon master: ${MASTER}" >&2; exit 1; }
command -v ffmpeg >/dev/null 2>&1 || { echo "ffmpeg is required" >&2; exit 1; }

master_edge="$(ffprobe -v error -show_entries stream=width -of csv=p=0 "${MASTER}")"
echo "master ${MASTER} (${master_edge}px)"

# name:edge, one per slot from ios 5 through 15
slots="Icon.png:57 Icon@2x.png:114 Icon-72.png:72 Icon-72@2x.png:144 \
Icon-60@2x.png:120 Icon-60@3x.png:180 Icon-76.png:76 Icon-76@2x.png:152 \
Icon-83.5@2x.png:167"

for slot in ${slots}; do
  name="${slot%%:*}"
  edge="${slot##*:}"
  ffmpeg -v error -y -i "${MASTER}" \
      -vf "scale=${edge}:${edge}:flags=lanczos" \
      "${ICONS}/${name}"
  if [ "${edge}" -gt "${master_edge}" ]; then
    printf '%-18s %4spx (upscaled from %spx)\n' "${name}" "${edge}" "${master_edge}"
  else
    printf '%-18s %4spx\n' "${name}" "${edge}"
  fi
done
