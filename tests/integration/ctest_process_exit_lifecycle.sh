#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="${1:-}"
if [[ -z "${ROOT_DIR}" ]]; then
  echo "usage: ctest_process_exit_lifecycle.sh <aivm-root>" >&2
  exit 2
fi

AILANG_BIN="${AILANG_BIN:-${ROOT_DIR}/../AiLang/tools/ailang}"
CASE_PATH="${ROOT_DIR}/tests/golden/parity_cases/vm_c_execute_src_process_exit_7.aos"
TMP_DIR="${ROOT_DIR}/.tmp/ctest-process-exit-lifecycle"

if [[ ! -x "${AILANG_BIN}" ]]; then
  echo "skip: missing ${AILANG_BIN}"
  exit 0
fi

rm -rf "${TMP_DIR}"
mkdir -p "${TMP_DIR}"
"${AILANG_BIN}" publish "${CASE_PATH}" --out "${TMP_DIR}/publish" >/dev/null

set +e
"${AILANG_BIN}" debug run "${TMP_DIR}/publish/app.aibc1" --out "${TMP_DIR}/debug" >"${TMP_DIR}/stdout" 2>"${TMP_DIR}/stderr"
STATUS=$?
set -e

if [[ "${STATUS}" -ne 7 ]]; then
  echo "process exit lifecycle: expected exit status 7, got ${STATUS}" >&2
  cat "${TMP_DIR}/stdout" >&2
  cat "${TMP_DIR}/stderr" >&2
  exit 1
fi
if [[ ! -f "${TMP_DIR}/debug/diagnostics.toml" ]]; then
  echo "process exit lifecycle: debug diagnostics were not finalized" >&2
  exit 1
fi
if ! grep -q 'status=ok' "${TMP_DIR}/debug/diagnostics.toml"; then
  echo "process exit lifecycle: expected completed VM diagnostics" >&2
  cat "${TMP_DIR}/debug/diagnostics.toml" >&2
  exit 1
fi
if ! grep -q 'exit_code = 7' "${TMP_DIR}/debug/config.toml"; then
  echo "process exit lifecycle: expected exit code in debug config" >&2
  cat "${TMP_DIR}/debug/config.toml" >&2
  exit 1
fi

echo "process exit lifecycle: PASS"
