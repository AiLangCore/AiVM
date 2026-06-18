#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PRESET="${AIVM_CMAKE_PRESET:-aivm-native-unix}"
TEST_LABEL="${AIVM_CTEST_LABEL:-unit}"

"${ROOT_DIR}/scripts/check-syscall-contracts.sh"

cmake --preset "${PRESET}" -S "${ROOT_DIR}/src"
cmake --build "${ROOT_DIR}/.tmp/aivm-c-build-native"
ctest --test-dir "${ROOT_DIR}/.tmp/aivm-c-build-native" -L "${TEST_LABEL}" --output-on-failure
"${ROOT_DIR}/scripts/test-aivm-debug-artifacts.sh" "${ROOT_DIR}/.tmp/aivm-c-build-native"

if [[ "${AIVM_RUN_DEBUG_MEMORY_SMOKE:-1}" == "1" ]]; then
  AILANG_ROOT="${AIVM_AILANG_ROOT:-${ROOT_DIR}/../AiLang}"
  bash "${ROOT_DIR}/src/tests/ctest_debug_memory_smoke.sh" "${AILANG_ROOT}"
fi
