#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${1:-${ROOT_DIR}/.tmp/aivm-c-build-native}"
AIVM_DEBUG="${BUILD_DIR}/aivm-debug"
TMP_DIR="${ROOT_DIR}/.tmp/aivm-debug-artifacts"

if [[ ! -x "${AIVM_DEBUG}" ]]; then
  echo "missing aivm-debug executable: ${AIVM_DEBUG}" >&2
  exit 1
fi

rm -rf "${TMP_DIR}"
mkdir -p "${TMP_DIR}"

FAIL_PROGRAM="${TMP_DIR}/pop-underflow.aibc1"
OK_PROGRAM="${TMP_DIR}/push-42.aibc1"
LOAD_FAIL_PROGRAM="${TMP_DIR}/bad-magic.aibc1"
FAIL_RUN="${TMP_DIR}/fail-run"
OK_RUN="${TMP_DIR}/ok-run"
LOAD_FAIL_RUN="${TMP_DIR}/load-fail-run"

printf '%b' \
  '\x41\x49\x42\x43\x02\x00\x00\x00\x00\x00\x00\x00\x01\x00\x00\x00'\
  '\x01\x00\x00\x00\x10\x00\x00\x00'\
  '\x01\x00\x00\x00'\
  '\x04\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00' \
  >"${FAIL_PROGRAM}"

printf '%b' \
  '\x41\x49\x42\x43\x02\x00\x00\x00\x00\x00\x00\x00\x01\x00\x00\x00'\
  '\x01\x00\x00\x00\x1c\x00\x00\x00'\
  '\x02\x00\x00\x00'\
  '\x03\x00\x00\x00\x2a\x00\x00\x00\x00\x00\x00\x00'\
  '\x01\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00' \
  >"${OK_PROGRAM}"

printf '%b' \
  '\x58\x49\x42\x43\x02\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00' \
  >"${LOAD_FAIL_PROGRAM}"

set +e
"${AIVM_DEBUG}" debug capture run "${LOAD_FAIL_PROGRAM}" --out "${LOAD_FAIL_RUN}" >"${TMP_DIR}/load-fail.stdout" 2>"${TMP_DIR}/load-fail.stderr"
load_fail_rc=$?
set -e

if [[ "${load_fail_rc}" -ne 2 ]]; then
  echo "expected load-failing capture rc=2, got ${load_fail_rc}" >&2
  exit 1
fi

set +e
"${AIVM_DEBUG}" debug capture run "${FAIL_PROGRAM}" --out "${FAIL_RUN}" >"${TMP_DIR}/fail.stdout" 2>"${TMP_DIR}/fail.stderr"
fail_rc=$?
set -e

if [[ "${fail_rc}" -ne 3 ]]; then
  echo "expected failing capture rc=3, got ${fail_rc}" >&2
  exit 1
fi

set +e
"${AIVM_DEBUG}" debug capture run "${OK_PROGRAM}" --out "${OK_RUN}" >"${TMP_DIR}/ok.stdout" 2>"${TMP_DIR}/ok.stderr"
ok_rc=$?
set -e

if [[ "${ok_rc}" -ne 42 ]]; then
  echo "expected ok capture rc=42, got ${ok_rc}" >&2
  exit 1
fi

for artifact in \
  config.toml \
  diagnostics.toml \
  stdout.txt \
  stderr.txt \
  vm_trace.toml \
  syscall_trace.toml \
  stack_trace.toml \
  profile.toml \
  memory.toml \
  suggestions.toml
do
  test -f "${LOAD_FAIL_RUN}/${artifact}"
  test -f "${FAIL_RUN}/${artifact}"
  test -f "${OK_RUN}/${artifact}"
done

grep -q 'phase = "load"' "${LOAD_FAIL_RUN}/diagnostics.toml"
grep -q 'runtime_profile = "debug"' "${LOAD_FAIL_RUN}/diagnostics.toml"
grep -q 'program_error_code = "AIVMP003"' "${LOAD_FAIL_RUN}/diagnostics.toml"
grep -q 'current_opcode = "LOAD_FAILED"' "${LOAD_FAIL_RUN}/stack_trace.toml"
grep -q 'instruction_count = 0' "${LOAD_FAIL_RUN}/profile.toml"
grep -q 'syscall_count = 0' "${LOAD_FAIL_RUN}/syscall_trace.toml"
grep -q 'limits = {' "${LOAD_FAIL_RUN}/memory.toml"
grep -q 'aivm: load failed:' "${LOAD_FAIL_RUN}/stderr.txt"

grep -q 'status = "error"' "${FAIL_RUN}/diagnostics.toml"
grep -q 'phase = "execute"' "${FAIL_RUN}/diagnostics.toml"
grep -q 'runtime_profile = "debug"' "${FAIL_RUN}/diagnostics.toml"
grep -q 'vm_error_code = "AIVM003"' "${FAIL_RUN}/diagnostics.toml"
grep -q 'current_pc = 0' "${FAIL_RUN}/stack_trace.toml"
grep -q 'current_opcode = "POP"' "${FAIL_RUN}/stack_trace.toml"
grep -q 'instruction_count = 1' "${FAIL_RUN}/profile.toml"
grep -q 'syscall_count = 0' "${FAIL_RUN}/profile.toml"
grep -q 'status = "error"' "${FAIL_RUN}/profile.toml"
grep -q 'aivm: execution failed:' "${FAIL_RUN}/stderr.txt"

grep -q 'status = "ok"' "${OK_RUN}/diagnostics.toml"
grep -q 'phase = "execute"' "${OK_RUN}/diagnostics.toml"
grep -q 'runtime_profile = "debug"' "${OK_RUN}/diagnostics.toml"
grep -q 'exit_code = 42' "${OK_RUN}/diagnostics.toml"
grep -q 'instruction_count = 2' "${OK_RUN}/profile.toml"
grep -q 'opcode = "PUSH_INT", count = 1' "${OK_RUN}/profile.toml"
grep -q 'opcode = "HALT", count = 1' "${OK_RUN}/profile.toml"

"${AIVM_DEBUG}" explain "${FAIL_RUN}" >"${TMP_DIR}/explain.txt"
grep -q 'runtime_profile: "debug"' "${TMP_DIR}/explain.txt"
grep -q 'phase: "execute"' "${TMP_DIR}/explain.txt"
grep -q 'vm_error_code: "AIVM003"' "${TMP_DIR}/explain.txt"
grep -q 'current_opcode: "POP"' "${TMP_DIR}/explain.txt"

"${AIVM_DEBUG}" explain "${LOAD_FAIL_RUN}" >"${TMP_DIR}/explain-load-fail.txt"
grep -q 'phase: "load"' "${TMP_DIR}/explain-load-fail.txt"
grep -q 'program_error_code: "AIVMP003"' "${TMP_DIR}/explain-load-fail.txt"
grep -q 'current_opcode: "LOAD_FAILED"' "${TMP_DIR}/explain-load-fail.txt"

"${AIVM_DEBUG}" inspect stack "${FAIL_RUN}" >"${TMP_DIR}/inspect-stack.txt"
grep -q 'current_opcode: "POP"' "${TMP_DIR}/inspect-stack.txt"

"${AIVM_DEBUG}" inspect profile "${OK_RUN}" >"${TMP_DIR}/inspect-profile.txt"
grep -q 'instruction_count: 2' "${TMP_DIR}/inspect-profile.txt"
grep -q 'opcode_count: .*"PUSH_INT".*count = 1' "${TMP_DIR}/inspect-profile.txt"

"${AIVM_DEBUG}" inspect syscalls "${OK_RUN}" >"${TMP_DIR}/inspect-syscalls.txt"
grep -q 'syscall_count: 0' "${TMP_DIR}/inspect-syscalls.txt"

"${AIVM_DEBUG}" suggest "${FAIL_RUN}" >"${TMP_DIR}/suggest.txt"
grep -q 'aivm-debug inspect stack' "${TMP_DIR}/suggest.txt"

"${AIVM_DEBUG}" suggest "${LOAD_FAIL_RUN}" >"${TMP_DIR}/suggest-load-fail.txt"
grep -q 'verify the input is a valid AiBC v2 file' "${TMP_DIR}/suggest-load-fail.txt"

"${AIVM_DEBUG}" compare "${FAIL_RUN}" "${OK_RUN}" >"${TMP_DIR}/compare.txt"
grep -q 'status: left="error" right="ok" changed=true' "${TMP_DIR}/compare.txt"
grep -q 'phase: left="execute" right="execute" changed=false' "${TMP_DIR}/compare.txt"
grep -q 'instruction_count: left=1 right=2 changed=true' "${TMP_DIR}/compare.txt"

echo "aivm debug artifacts: PASS"
