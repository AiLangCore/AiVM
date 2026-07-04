#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
WORKSPACE_DIR="$(cd "${ROOT_DIR}/.." && pwd)"
AILANG_BIN="${AILANG_BIN:-${WORKSPACE_DIR}/AiLang/tools/ailang}"
TMP_DIR="${ROOT_DIR}/.tmp/external-command-dispatch"
SDK_DIR="${TMP_DIR}/sdk"
COMMAND_DIR="${SDK_DIR}/libexec/ailang/commands"

if [[ ! -x "${AILANG_BIN}" ]]; then
  echo "missing AILANG_BIN: ${AILANG_BIN}" >&2
  exit 1
fi

rm -rf "${TMP_DIR}"
mkdir -p "${COMMAND_DIR}"

cat >"${COMMAND_DIR}/probe" <<'EOF_PROBE'
#!/usr/bin/env sh
printf 'external-probe'
for arg in "$@"; do
  printf ':%s' "$arg"
done
printf '\n'
EOF_PROBE
chmod +x "${COMMAND_DIR}/probe"

output="$(
  AILANG_SDK_ROOT="${SDK_DIR}" "${AILANG_BIN}" probe one two-three
)"

if [[ "${output}" != "external-probe:one:two-three" ]]; then
  echo "external command dispatch failed" >&2
  echo "expected: external-probe:one:two-three" >&2
  echo "actual:   ${output}" >&2
  exit 1
fi

echo "external command dispatch smoke passed."
