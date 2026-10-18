#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SRC="$(cd "${1:?usage: prepare_core_overlay.sh core-source staging-directory}" && pwd)"
STAGE="${2:?set the overlay staging directory}"
mkdir -p "${STAGE}/core/proxy/tun"
STAGE="$(cd "${STAGE}" && pwd)"
for source in udp_fullcone.go stack_gvisor.go; do
  cp "${SRC}/proxy/tun/${source}" "${STAGE}/core/proxy/tun/${source}"
done
patch -s -p1 -d "${STAGE}/core" < "${ROOT}/scripts/patches/senko-core-tun-udp.patch"
python3 - "${SRC}" "${STAGE}" <<'PY'
import json
from pathlib import Path
import sys

source, stage = map(Path, sys.argv[1:])
path = stage / "overlay.json"
overlay = json.loads(path.read_text()) if path.exists() else {"Replace": {}}
for name in ("udp_fullcone.go", "stack_gvisor.go"):
    relative = Path("proxy/tun") / name
    overlay["Replace"][str(source / relative)] = str(stage / "core" / relative)
path.write_text(json.dumps(overlay) + "\n")
PY
