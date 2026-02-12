#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${ROOT_DIR}/build/fast_tests"
BUILD_TYPE="Release"
GENERATOR=""
CMAKE_ARGS=()

usage() {
  cat <<'EOT'
Usage: scripts/fast_tests.sh [options]
Options:
  --debug           Use Debug build type
  --relwithdebinfo  Use RelWithDebInfo build type
  --release         Use Release build type (default)
  --clean           Remove build directory before configuring
  --no-ccache       Disable ccache launcher
  -h, --help        Show this help
EOT
}

CLEAN=0
USE_CCACHE=1

while [[ $# -gt 0 ]]; do
  case "$1" in
    --debug) BUILD_TYPE="Debug"; shift ;;
    --relwithdebinfo) BUILD_TYPE="RelWithDebInfo"; shift ;;
    --release) BUILD_TYPE="Release"; shift ;;
    --clean) CLEAN=1; shift ;;
    --no-ccache) USE_CCACHE=0; shift ;;
    -h|--help) usage; exit 0 ;;
    *) echo "unknown argument: $1" >&2; usage; exit 1 ;;
  esac
done

if [[ $CLEAN -eq 1 ]]; then
  rm -rf "${BUILD_DIR}"
fi

if command -v ninja >/dev/null 2>&1; then
  GENERATOR="-G Ninja"
fi

if [[ $USE_CCACHE -eq 1 ]] && command -v ccache >/dev/null 2>&1; then
  CMAKE_ARGS+=("-DCMAKE_CXX_COMPILER_LAUNCHER=ccache")
fi

cmake -S "${ROOT_DIR}" -B "${BUILD_DIR}" ${GENERATOR} \
  -DCMAKE_BUILD_TYPE="${BUILD_TYPE}" \
  "${CMAKE_ARGS[@]}"

cmake --build "${BUILD_DIR}" --target simplevm_core_obj simplevm simplevm_tests --parallel

TEST_EXE=""
for p in \
  "${BUILD_DIR}/bin/simplevm_tests" \
  "${BUILD_DIR}/bin/Release/simplevm_tests" \
  "${BUILD_DIR}/Release/simplevm_tests"; do
  if [[ -x "$p" ]]; then
    TEST_EXE="$p"
    break
  fi
done

if [[ -z "$TEST_EXE" ]]; then
  echo "simplevm_tests not found in build output" >&2
  exit 1
fi

if [[ -x "${BUILD_DIR}/bin/simplevm" ]]; then
  mkdir -p "${ROOT_DIR}/bin" "${ROOT_DIR}/build/bin"
  cp "${BUILD_DIR}/bin/simplevm" "${ROOT_DIR}/build/bin/simplevm"
  cp "${BUILD_DIR}/bin/simplevm" "${ROOT_DIR}/bin/simplevm"
  cp "${BUILD_DIR}/bin/simplevm" "${ROOT_DIR}/bin/simple"
fi

"$TEST_EXE"
