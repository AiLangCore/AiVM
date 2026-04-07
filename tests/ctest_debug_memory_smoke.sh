#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="${1:-}"
if [[ -z "${ROOT_DIR}" ]]; then
  echo "usage: ctest_debug_memory_smoke.sh <repo-root>" >&2
  exit 2
fi

AIRUN_BIN="${ROOT_DIR}/tools/airun"
if [[ ! -x "${AIRUN_BIN}" ]]; then
  echo "skip: missing ${AIRUN_BIN}"
  exit 0
fi

TMP_NATIVE_DEBUG_MEM_DIR="${ROOT_DIR}/.tmp/ctest-debug-memory"
TMP_NATIVE_DEBUG_MEM_OUT="${ROOT_DIR}/.tmp/ctest-debug-memory-out"
TMP_NATIVE_DEBUG_MEM_APP="${TMP_NATIVE_DEBUG_MEM_DIR}/memory_pressure.aos"
rm -rf "${TMP_NATIVE_DEBUG_MEM_DIR}" "${TMP_NATIVE_DEBUG_MEM_OUT}"
mkdir -p "${TMP_NATIVE_DEBUG_MEM_DIR}"
{
  echo 'Bytecode#bc1(magic="AIBC" format="AiBC1" version=2 flags=0) {'
  echo '  Const#k0(kind=string value="n")'
  echo '  Func#f1(name=main params="argv" locals="") {'
  n=1
  inst_id=1
  while [[ $n -le 600 ]]; do
    echo "    Inst#c${inst_id}(op=CONST a=0)"
    inst_id=$((inst_id + 1))
    echo "    Inst#m${inst_id}(op=MAKE_BLOCK)"
    inst_id=$((inst_id + 1))
    n=$((n + 1))
  done
  echo "    Inst#h${inst_id}(op=HALT)"
  echo '  }'
  echo '}'
} > "${TMP_NATIVE_DEBUG_MEM_APP}"

if "${AIRUN_BIN}" debug run "${TMP_NATIVE_DEBUG_MEM_APP}" --out "${TMP_NATIVE_DEBUG_MEM_OUT}" >/dev/null 2>&1; then
  echo "debug memory smoke: expected memory-pressure failure" >&2
  exit 1
fi

if [[ ! -f "${TMP_NATIVE_DEBUG_MEM_OUT}/diagnostics.toml" || ! -f "${TMP_NATIVE_DEBUG_MEM_OUT}/state_snapshots.toml" || ! -f "${TMP_NATIVE_DEBUG_MEM_OUT}/config.toml" ]]; then
  echo "debug memory smoke: expected debug artifacts missing" >&2
  exit 1
fi
if ! grep -q 'status = "error"' "${TMP_NATIVE_DEBUG_MEM_OUT}/config.toml"; then
  echo "debug memory smoke: expected status=error in config.toml" >&2
  exit 1
fi
if ! grep -q "vm_code=AIVM011" "${TMP_NATIVE_DEBUG_MEM_OUT}/diagnostics.toml"; then
  echo "debug memory smoke: expected vm_code=AIVM011" >&2
  exit 1
fi
if ! grep -Eq "detail=(AIVMM005: )?node arena capacity exceeded\\." "${TMP_NATIVE_DEBUG_MEM_OUT}/diagnostics.toml"; then
  echo "debug memory smoke: expected node arena capacity detail" >&2
  exit 1
fi
if ! grep -Eq "node_gc_compactions = [1-9][0-9]*" "${TMP_NATIVE_DEBUG_MEM_OUT}/diagnostics.toml"; then
  echo "debug memory smoke: expected gc compaction activity" >&2
  exit 1
fi
if ! grep -Eq "node_gc_attempts = [1-9][0-9]*" "${TMP_NATIVE_DEBUG_MEM_OUT}/diagnostics.toml"; then
  echo "debug memory smoke: expected gc attempt activity" >&2
  exit 1
fi
if ! grep -q "node_count = 512" "${TMP_NATIVE_DEBUG_MEM_OUT}/diagnostics.toml"; then
  echo "debug memory smoke: expected node_count=512 in diagnostics.toml" >&2
  exit 1
fi
if ! grep -q "node_high_water = 512" "${TMP_NATIVE_DEBUG_MEM_OUT}/diagnostics.toml"; then
  echo "debug memory smoke: expected node_high_water=512 in diagnostics.toml" >&2
  exit 1
fi
if ! grep -q "node_gc_pressure_threshold_nodes = 384" "${TMP_NATIVE_DEBUG_MEM_OUT}/diagnostics.toml"; then
  echo "debug memory smoke: expected node_gc_pressure_threshold_nodes=384 in diagnostics.toml" >&2
  exit 1
fi
if ! grep -q "node_roots = {" "${TMP_NATIVE_DEBUG_MEM_OUT}/diagnostics.toml"; then
  echo "debug memory smoke: expected node_roots table in diagnostics.toml" >&2
  exit 1
fi
if ! grep -Eq "node_gc_attempts = [1-9][0-9]*" "${TMP_NATIVE_DEBUG_MEM_OUT}/state_snapshots.toml"; then
  echo "debug memory smoke: expected node_gc_attempts>0 in state snapshots" >&2
  exit 1
fi
if ! grep -q "node_root_stack_slots" "${TMP_NATIVE_DEBUG_MEM_OUT}/state_snapshots.toml"; then
  echo "debug memory smoke: expected node_root_stack_slots in state snapshots" >&2
  exit 1
fi

TMP_NATIVE_DEBUG_OK_DIR="${ROOT_DIR}/.tmp/ctest-debug-ok"
TMP_NATIVE_DEBUG_OK_OUT="${ROOT_DIR}/.tmp/ctest-debug-ok-out"
TMP_NATIVE_DEBUG_OK_APP="${TMP_NATIVE_DEBUG_OK_DIR}/success_path.aos"
rm -rf "${TMP_NATIVE_DEBUG_OK_DIR}" "${TMP_NATIVE_DEBUG_OK_OUT}"
mkdir -p "${TMP_NATIVE_DEBUG_OK_DIR}"
cat > "${TMP_NATIVE_DEBUG_OK_APP}" <<'EOF'
Bytecode#bc1(magic="AIBC" format="AiBC1" version=2 flags=0) {
  Func#f1(name=main params="argv" locals="") {
    Inst#i1(op=HALT)
  }
}
EOF
if ! "${AIRUN_BIN}" debug run "${TMP_NATIVE_DEBUG_OK_APP}" --out "${TMP_NATIVE_DEBUG_OK_OUT}" >/dev/null 2>&1; then
  echo "debug memory smoke: expected successful debug run" >&2
  exit 1
fi
if ! grep -q 'status = "ok"' "${TMP_NATIVE_DEBUG_OK_OUT}/config.toml"; then
  echo "debug memory smoke: expected status=ok in config.toml" >&2
  exit 1
fi
if ! grep -q "vm_code=AIVM000" "${TMP_NATIVE_DEBUG_OK_OUT}/diagnostics.toml"; then
  echo "debug memory smoke: expected vm_code=AIVM000" >&2
  exit 1
fi
if ! grep -q "node_gc_attempts = 0" "${TMP_NATIVE_DEBUG_OK_OUT}/diagnostics.toml"; then
  echo "debug memory smoke: expected node_gc_attempts=0 in diagnostics.toml" >&2
  exit 1
fi
if ! grep -q "node_root_stack_slots" "${TMP_NATIVE_DEBUG_OK_OUT}/state_snapshots.toml"; then
  echo "debug memory smoke: expected node_root_stack_slots in success state snapshots" >&2
  exit 1
fi
if ! grep -q "node_roots = {" "${TMP_NATIVE_DEBUG_OK_OUT}/diagnostics.toml"; then
  echo "debug memory smoke: expected node_roots table in success diagnostics.toml" >&2
  exit 1
fi

echo "debug memory smoke: PASS"
