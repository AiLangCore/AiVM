#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PRESET="${AIVM_CMAKE_PRESET:-aivm-native-unix}"
TEST_LABEL="${AIVM_CTEST_LABEL:-unit}"

cmake --preset "${PRESET}" -S "${ROOT_DIR}/native"
cmake --build "${ROOT_DIR}/.tmp/aivm-c-build-native"
ctest --test-dir "${ROOT_DIR}/.tmp/aivm-c-build-native" -L "${TEST_LABEL}" --output-on-failure
