#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
CLI_SOURCE="${ROOT_DIR}/src/ailang_cli/ailang.c"

CLOSE_BODY="$(
  sed -n \
    '/static int airun_queue_injected_close(void)/,/^}/p' \
    "${CLI_SOURCE}"
)"

printf '%s\n' "${CLOSE_BODY}" | grep -Fq '"closed"'
if printf '%s\n' "${CLOSE_BODY}" | grep -Fq '"close"'; then
  echo "debug close injection must use the canonical closed event type" >&2
  exit 1
fi

if rg -n 'sys\.net\.asyncCancel' "${ROOT_DIR}/src"; then
  echo "obsolete async cancel syscall alias remains in AiVM source" >&2
  exit 1
fi

echo "debug event contracts: PASS"
