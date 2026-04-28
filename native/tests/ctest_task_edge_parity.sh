#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="${1:-}"
PARITY_CLI_BIN="${2:-}"

if [[ -z "${ROOT_DIR}" || -z "${PARITY_CLI_BIN}" ]]; then
  echo "usage: ctest_task_edge_parity.sh <repo-root> <aivm_parity_cli>" >&2
  exit 2
fi
if [[ ! -x "${PARITY_CLI_BIN}" ]]; then
  echo "missing parity cli: ${PARITY_CLI_BIN}" >&2
  exit 2
fi

AIRUN_BIN="${ROOT_DIR}/tools/airun"
if [[ ! -x "${AIRUN_BIN}" ]]; then
  echo "skip: missing ${AIRUN_BIN}"
  exit 0
fi

TMP_DIR="${ROOT_DIR}/.tmp/ctest-task-edge-parity"
rm -rf "${TMP_DIR}"
mkdir -p "${TMP_DIR}"

run_case() {
  local name="$1"
  local input="$2"
  local expected="$3"
  local expected_exit="$4"
  local actual="${TMP_DIR}/${name}.out"
  local actual_exit=0

  set +e
  "${AIRUN_BIN}" run "${input}" --vm=c > "${actual}" 2>&1
  actual_exit=$?
  set -e

  if [[ "${actual_exit}" != "${expected_exit}" ]]; then
    echo "task edge parity mismatch (${name}): exit ${actual_exit} expected ${expected_exit}" >&2
    cat "${actual}" >&2
    exit 1
  fi
  if [[ -n "${expected}" ]]; then
    if ! "${PARITY_CLI_BIN}" "${actual}" "${expected}" >/dev/null 2>&1; then
      echo "task edge parity mismatch (${name}): output differs" >&2
      cat "${actual}" >&2
      exit 1
    fi
  else
    if [[ -s "${actual}" ]]; then
      echo "task edge parity mismatch (${name}): expected empty output" >&2
      cat "${actual}" >&2
      exit 1
    fi
  fi
}

run_case \
  "await_edge_invalid" \
  "${ROOT_DIR}/src/AiVM.Core/native/tests/parity_cases/vm_c_execute_src_await_edge_invalid.aos" \
  "${ROOT_DIR}/src/AiVM.Core/native/tests/parity_cases/vm_c_execute_src_await_edge_invalid.out" \
  "3"
run_case \
  "par_join_edge_invalid" \
  "${ROOT_DIR}/src/AiVM.Core/native/tests/parity_cases/vm_c_execute_src_par_join_edge_invalid.aos" \
  "${ROOT_DIR}/src/AiVM.Core/native/tests/parity_cases/vm_c_execute_src_par_join_edge_invalid.out" \
  "3"
run_case \
  "par_cancel_edge_noop" \
  "${ROOT_DIR}/src/AiVM.Core/native/tests/parity_cases/vm_c_execute_src_par_cancel_edge_noop.aos" \
  "" \
  "0"

echo "task edge parity: PASS"
