#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
WORKSPACE_DIR="$(cd "${ROOT_DIR}/.." && pwd)"
AILANG_BIN="${AILANG_BIN:-${WORKSPACE_DIR}/AiLang/tools/ailang}"
AILANG_PACKAGE_REGISTRY="${AILANG_PACKAGE_REGISTRY:-${WORKSPACE_DIR}/ailang-packages}"
TMP_DIR="${ROOT_DIR}/.tmp/target-package-dispatch"

if [[ ! -x "${AILANG_BIN}" ]]; then
  echo "missing AILANG_BIN: ${AILANG_BIN}" >&2
  exit 1
fi
if [[ ! -d "${AILANG_PACKAGE_REGISTRY}/packages" ]]; then
  echo "missing AILANG_PACKAGE_REGISTRY packages directory: ${AILANG_PACKAGE_REGISTRY}" >&2
  exit 1
fi

run_case() {
  local package_name="$1"
  local target_id="$2"
  local package_version="$3"
  local expected_message="$4"
  local case_dir="${TMP_DIR}/${package_name}-${target_id}"
  local rc
  shift 4

  rm -rf "${case_dir}"
  mkdir -p "${case_dir}"
  cat >"${case_dir}/project.aiproj" <<EOF_PROJECT
Program {
  Project(name="TargetPackageDispatch" version="0.0.1" entryFile="app.aos") {
    Include(name="${package_name}" version="${package_version}")
  }
}
EOF_PROJECT
  cat >"${case_dir}/app.aos" <<'EOF_APP'
Program#target_package_dispatch {
  Main#main {
    Return#ok(value=0)
  }
}
EOF_APP

  (
    cd "${case_dir}"
    AILANG_PACKAGE_REGISTRY="${AILANG_PACKAGE_REGISTRY}" "${AILANG_BIN}" package restore >/dev/null
    set +e
    AILANG_PACKAGE_REGISTRY="${AILANG_PACKAGE_REGISTRY}" "${AILANG_BIN}" run . --target "${target_id}" "$@" >run.out 2>run.err
    rc=$?
    set -e
    if [[ "${rc}" -eq 0 ]]; then
      echo "expected package target runner to return non-zero for ${package_name}/${target_id}" >&2
      cat run.out
      cat run.err >&2
      exit 1
    fi
    if ! grep -Fq "${expected_message}" run.err; then
      echo "package target runner was not invoked for ${package_name}/${target_id}" >&2
      cat run.out
      cat run.err >&2
      exit 1
    fi
  )
}

run_case "target-macos" "macos" "0.0.1-alpha.2" "Unsupported macOS target arch." --target-option arch=bogus
run_case "target-windows" "windows" "0.0.1-alpha.2" "target-windows run requires a Windows host"
run_case "target-linux" "linux" "0.0.1-alpha.11" "target-linux run requires a Linux host"
run_case "target-wasm" "wasm32" "0.0.1-alpha.3" "Unsupported wasm profile." --target-option wasm-profile=bogus

echo "target package dispatch smoke passed."
