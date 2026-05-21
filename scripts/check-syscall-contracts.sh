#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
CONTRACTS_FILE="${ROOT_DIR}/native/sys/aivm_syscall_contracts.c"
DOCS_FILE="${ROOT_DIR}/Docs/Syscalls.md"
TEST_FILE="${ROOT_DIR}/native/tests/test_syscall_contracts.c"
TMP_DIR="${ROOT_DIR}/.tmp/syscall-contract-check"
CONTRACT_LIST="${TMP_DIR}/contracts.txt"
DETERMINISTIC_ACTUAL="${TMP_DIR}/deterministic-utility-contracts.actual.txt"
DETERMINISTIC_ALLOWED="${TMP_DIR}/deterministic-utility-contracts.allowed.txt"

mkdir -p "${TMP_DIR}"

sed -n 's/^[[:space:]]*{[[:space:]]*\([0-9][0-9]*\)U,[[:space:]]*"\([^"]*\)".*/\1 \2/p' \
  "${CONTRACTS_FILE}" > "${CONTRACT_LIST}"

if [ ! -s "${CONTRACT_LIST}" ]; then
  echo "syscall check: no syscall contracts found in ${CONTRACTS_FILE}" >&2
  exit 1
fi

duplicate_ids="$(cut -d ' ' -f 1 "${CONTRACT_LIST}" | sort | uniq -d)"
if [ -n "${duplicate_ids}" ]; then
  echo "syscall check: duplicate syscall IDs:" >&2
  echo "${duplicate_ids}" >&2
  exit 1
fi

duplicate_targets="$(cut -d ' ' -f 2- "${CONTRACT_LIST}" | sort | uniq -d)"
if [ -n "${duplicate_targets}" ]; then
  echo "syscall check: duplicate syscall targets:" >&2
  echo "${duplicate_targets}" >&2
  exit 1
fi

if [ ! -f "${DOCS_FILE}" ]; then
  echo "syscall check: missing ${DOCS_FILE}" >&2
  exit 1
fi

missing_docs=0
missing_tests=0
missing_capability=0
while read -r _id target; do
  if ! grep -F "\"${target}\"" "${CONTRACTS_FILE}" | grep -q "AIVM_SYSCALL_CAPABILITY_"; then
    echo "syscall check: ${target} is missing a syscall capability group" >&2
    missing_capability=1
  fi
  if ! grep -qF "\`${target}\`" "${DOCS_FILE}"; then
    echo "syscall check: ${target} is missing from Docs/Syscalls.md" >&2
    missing_docs=1
  fi
  if ! grep -qF "\"${target}\"" "${TEST_FILE}"; then
    echo "syscall check: ${target} is missing from native/tests/test_syscall_contracts.c" >&2
    missing_tests=1
  fi
done < "${CONTRACT_LIST}"

if [ "${missing_capability}" -ne 0 ] || [ "${missing_docs}" -ne 0 ] || [ "${missing_tests}" -ne 0 ]; then
  exit 1
fi

cut -d ' ' -f 2- "${CONTRACT_LIST}" | grep -E '^sys\.(str|bytes)\.' | sort > "${DETERMINISTIC_ACTUAL}" || true
cat > "${DETERMINISTIC_ALLOWED}" <<'EOF'
sys.bytes.at
sys.bytes.concat
sys.bytes.fromBase64
sys.bytes.fromUtf8String
sys.bytes.length
sys.bytes.slice
sys.bytes.toBase64
sys.bytes.toUtf8String
sys.str.decodeUnicodeHex4
sys.str.decodeUnicodeSurrogatePairHex4
sys.str.find
sys.str.fromCodePoint
sys.str.remove
sys.str.substring
sys.str.utf8ByteCount
EOF

if ! diff -u "${DETERMINISTIC_ALLOWED}" "${DETERMINISTIC_ACTUAL}" >/dev/null; then
  echo "syscall check: deterministic utility syscall surface changed" >&2
  echo "syscall check: sys.str.* and sys.bytes.* are temporary migration contracts; do not add new ones" >&2
  diff -u "${DETERMINISTIC_ALLOWED}" "${DETERMINISTIC_ACTUAL}" >&2 || true
  exit 1
fi

echo "syscall check: PASS"
