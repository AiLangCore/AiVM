#!/usr/bin/env bash
set -euo pipefail

if [[ $# -lt 1 ]]; then
  echo "usage: ctest_wasm_golden.sh <repo-root>" >&2
  exit 2
fi

REPO_ROOT="$1"

if ! command -v emcc >/dev/null 2>&1 || ! command -v wasmtime >/dev/null 2>&1; then
  echo "skipping wasm golden ctest: emcc and/or wasmtime not found"
  exit 77
fi

"${REPO_ROOT}/scripts/test-wasm-golden.sh"
