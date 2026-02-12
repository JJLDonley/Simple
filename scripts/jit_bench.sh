#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${ROOT_DIR}/build/jit_bench"
BUILD_TYPE="Release"
GENERATOR=""
CMAKE_ARGS=()
ITERATIONS=1000
USE_CCACHE=1
CLEAN=0
MODE="bench"

JIT_TIER0=""
JIT_TIER1=""
JIT_OPCODE=""

usage() {
  cat <<'EOT'
Usage: scripts/jit_bench.sh [options]
Options:
  --iters N         Number of iterations per bench case (default: 1000)
  --hot             Run hot-loop bench (steady-state compiled path)
  --tier0 N         Override SIMPLE_JIT_TIER0
  --tier1 N         Override SIMPLE_JIT_TIER1
  --opcode N        Override SIMPLE_JIT_OPCODE
  --debug           Use Debug build type
  --relwithdebinfo  Use RelWithDebInfo build type
  --release         Use Release build type (default)
  --clean           Remove build directory before configuring
  --no-ccache       Disable ccache launcher
  -h, --help        Show this help
EOT
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --iters)
      ITERATIONS="${2:-}"; shift 2 ;;
    --hot)
      MODE="bench-hot"; shift ;;
    --tier0)
      JIT_TIER0="${2:-}"; shift 2 ;;
    --tier1)
      JIT_TIER1="${2:-}"; shift 2 ;;
    --opcode)
      JIT_OPCODE="${2:-}"; shift 2 ;;
    --debug) BUILD_TYPE="Debug"; shift ;;
    --relwithdebinfo) BUILD_TYPE="RelWithDebInfo"; shift ;;
    --release) BUILD_TYPE="Release"; shift ;;
    --clean) CLEAN=1; shift ;;
    --no-ccache) USE_CCACHE=0; shift ;;
    -h|--help) usage; exit 0 ;;
    *) echo "unknown argument: $1" >&2; usage; exit 1 ;;
  esac
done

if [[ -z "${ITERATIONS}" ]]; then
  echo "--iters requires a value" >&2
  usage
  exit 1
fi

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

cmake --build "${BUILD_DIR}" --target simplevm_tests --parallel

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

if [[ -n "${JIT_TIER0}" ]]; then
  export SIMPLE_JIT_TIER0="${JIT_TIER0}"
fi
if [[ -n "${JIT_TIER1}" ]]; then
  export SIMPLE_JIT_TIER1="${JIT_TIER1}"
fi
if [[ -n "${JIT_OPCODE}" ]]; then
  export SIMPLE_JIT_OPCODE="${JIT_OPCODE}"
fi

"$TEST_EXE" --${MODE} "${ITERATIONS}"
