#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SRC="$(cd "${SENKO_CORE_SRC:?set SENKO_CORE_SRC to the pinned senko-core source}" && pwd)"
GO_BIN="${SENKO_GO:?set SENKO_GO to the Go executable}"
if [[ "$(git -C "${SRC}" rev-parse HEAD)" != "2ec1657c03a4c368bfabc31e04df0d2c3f1d50a5" ]]; then
  echo "native tests require the pinned senko-core source" >&2
  exit 1
fi
STAGE="$(mktemp -d "${TMPDIR:-/tmp}/senko-core-tests.XXXXXX")"
trap 'rm -rf "${STAGE}"' EXIT
bash "${ROOT}/scripts/prepare_core_overlay.sh" "${SRC}" "${STAGE}"
python3 - "${ROOT}" "${SRC}" "${STAGE}/overlay.json" <<'PY'
import json
from pathlib import Path
import sys

root, source, path = map(Path, sys.argv[1:])
overlay = json.loads(path.read_text())
for test in (root / "native/tests").glob("*_test.go"):
    overlay["Replace"][str(source / "proxy/tun" / ("senko_" + test.name))] = str(test)
path.write_text(json.dumps(overlay) + "\n")
PY
cd "${SRC}"
"${GO_BIN}" test -race -overlay "${STAGE}/overlay.json" -tags senko_trim ./proxy/tun -count=1
