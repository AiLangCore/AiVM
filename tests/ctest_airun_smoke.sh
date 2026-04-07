#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="${1:-}"
if [[ -z "${ROOT_DIR}" ]]; then
  echo "usage: ctest_airun_smoke.sh <repo-root>" >&2
  exit 2
fi

AIRUN_BIN="${ROOT_DIR}/tools/airun"
if [[ ! -x "${AIRUN_BIN}" ]]; then
  echo "skip: missing ${AIRUN_BIN}"
  exit 0
fi

TMP_DIR="${ROOT_DIR}/.tmp/ctest-airun-smoke"
rm -rf "${TMP_DIR}"
mkdir -p "${TMP_DIR}"

CASE_PATH="${ROOT_DIR}/src/AiVM.Core/native/tests/parity_cases/vm_c_execute_src_main_params.aos"
"${AIRUN_BIN}" run "${CASE_PATH}" --vm=c >/dev/null

PUBLISH_DIR="${TMP_DIR}/publish-main-params"
PUBLISH_ERR="${TMP_DIR}/publish-main-params.err"
if ! "${AIRUN_BIN}" publish "${CASE_PATH}" --out "${PUBLISH_DIR}" >/dev/null 2>"${PUBLISH_ERR}"; then
  if grep -q "Failed to copy runtime for target RID" "${PUBLISH_ERR}"; then
    echo "airun smoke: skipping native publish checks (runtime RID artifact missing)"
    exit 0
  fi
  cat "${PUBLISH_ERR}" >&2
  exit 1
fi
if [[ ! -f "${PUBLISH_DIR}/app.aibc1" ]]; then
  echo "airun smoke: publish missing app.aibc1" >&2
  exit 1
fi
if [[ ! -x "${PUBLISH_DIR}/vm_c_execute_src_main_params" ]]; then
  echo "airun smoke: publish missing direct app executable" >&2
  exit 1
fi
if [[ -e "${PUBLISH_DIR}/run.sh" || -e "${PUBLISH_DIR}/run.ps1" || -e "${PUBLISH_DIR}/run.cmd" ]]; then
  echo "airun smoke: unexpected launcher files for native publish" >&2
  exit 1
fi
"${PUBLISH_DIR}/vm_c_execute_src_main_params" >/dev/null

case "$(uname -s)" in
  Darwin) host_os="osx" ;;
  Linux) host_os="linux" ;;
  *) host_os="" ;;
esac
case "$(uname -m)" in
  arm64|aarch64) host_arch="arm64" ;;
  x86_64|amd64) host_arch="x64" ;;
  *) host_arch="" ;;
esac
if [[ -n "${host_os}" && -n "${host_arch}" ]]; then
  HOST_RID="${host_os}-${host_arch}"
  PROJECT_DIR="${TMP_DIR}/project-target"
  PROJECT_OUT="${TMP_DIR}/project-target-out"
  mkdir -p "${PROJECT_DIR}"
  cat > "${PROJECT_DIR}/project.aiproj" <<EOF
Program#p1 {
  Project#proj1(name="projtarget" entryFile="main.aos" entryExport="main" publishTarget="${HOST_RID}")
}
EOF
  cat > "${PROJECT_DIR}/main.aos" <<'EOF'
Bytecode#bc1(magic="AIBC" format="AiBC1" version=2 flags=0) {
  Func#f1(name=main params="argv" locals="") {
    Inst#i1(op=HALT)
  }
}
EOF
  PROJECT_ERR="${TMP_DIR}/project-target.err"
  if ! "${AIRUN_BIN}" publish "${PROJECT_DIR}" --out "${PROJECT_OUT}" >/dev/null 2>"${PROJECT_ERR}"; then
    if grep -q "Failed to copy runtime for target RID" "${PROJECT_ERR}"; then
      echo "airun smoke: skipping publishTarget manifest check (runtime RID artifact missing)"
    else
      cat "${PROJECT_ERR}" >&2
      exit 1
    fi
  elif [[ ! -x "${PROJECT_OUT}/projtarget" ]]; then
    echo "airun smoke: publishTarget manifest did not emit projtarget executable" >&2
    exit 1
  fi
fi

AIRUN_HELP="$("${AIRUN_BIN}" 2>&1 || true)"
if grep -Eq '^[[:space:]]+build([[:space:]]|$)' <<<"${AIRUN_HELP}" &&
   grep -Eq '^[[:space:]]+clean([[:space:]]|$)' <<<"${AIRUN_HELP}"; then
  CACHE_PROJECT="${TMP_DIR}/cache-project"
  CACHE_OUT_NO="${TMP_DIR}/cache-out-no"
  CACHE_OUT_YES="${TMP_DIR}/cache-out-yes"
  mkdir -p "${CACHE_PROJECT}"
  cat > "${CACHE_PROJECT}/project.aiproj" <<'EOF'
Program#p1 {
  Project#proj1(name="cache_smoke" entryFile="app.aos" entryExport="start")
}
EOF
  cat > "${CACHE_PROJECT}/app.aos" <<'EOF'
Program#p1 {
  Export#e1(name=start)
  Let#l1(name=start) {
    Fn#f1(params=argv) {
      Block#b1 {
        Return#r1 { Lit#i1(value=0) }
      }
    }
  }
}
EOF

  "${AIRUN_BIN}" clean "${CACHE_PROJECT}" >/dev/null
  "${AIRUN_BIN}" build --no-cache "${CACHE_PROJECT}" --out "${CACHE_OUT_NO}" >/dev/null
  if find "${CACHE_PROJECT}/.toolchain/cache/airun" -type f -name app.aibc1 2>/dev/null | grep -q .; then
    echo "airun smoke: --no-cache unexpectedly populated cache" >&2
    exit 1
  fi
  "${AIRUN_BIN}" build "${CACHE_PROJECT}" --out "${CACHE_OUT_YES}" >/dev/null
  if ! find "${CACHE_PROJECT}/.toolchain/cache/airun" -type f -name app.aibc1 2>/dev/null | grep -q .; then
    echo "airun smoke: cached build did not write cache artifact" >&2
    exit 1
  fi
  "${AIRUN_BIN}" clean "${CACHE_PROJECT}" >/dev/null
  if find "${CACHE_PROJECT}/.toolchain/cache/airun" -type f -name app.aibc1 2>/dev/null | grep -q .; then
    echo "airun smoke: clean did not remove cache artifact" >&2
    exit 1
  fi
fi

echo "airun smoke: PASS"
