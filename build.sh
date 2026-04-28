#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

case "${1:-host}" in
  host)
    cmake --preset aivm-native-unix -S "${ROOT_DIR}/native"
    cmake --build "${ROOT_DIR}/.tmp/aivm-c-build-native"
    ;;
  shared)
    cmake --preset aivm-native-shared-unix -S "${ROOT_DIR}/native"
    cmake --build "${ROOT_DIR}/.tmp/aivm-c-build-shared-native"
    ;;
  -h|--help|help)
    printf '%s\n' 'Usage: ./build.sh [host|shared]'
    ;;
  *)
    printf 'unknown build target: %s\n' "$1" >&2
    exit 1
    ;;
esac
