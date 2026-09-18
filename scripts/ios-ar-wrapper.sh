#!/usr/bin/env bash
set -euo pipefail

exec "${SENKO_LLVM_AR:?set SENKO_LLVM_AR to llvm-ar}" "$@"
