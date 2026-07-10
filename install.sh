#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="${BUILD_DIR:-$ROOT_DIR/build}"
CONFIG="${CONFIG:-Release}"
JOBS="${JOBS:-2}"
BINDIR="${BINDIR:-$HOME/.local/bin}"
LLVM_JIT="${SIMPLEVM_ENABLE_LLVM_JIT:-${LLVM_JIT:-OFF}}"

usage() {
  cat <<EOF
usage: install.sh [options]

Build Simple, copy binaries to Compiler/bin, then install them to ~/.local/bin.

Options:
  --llvm-jit           Build with LLVM ORC JIT enabled
  --no-llvm-jit        Build without LLVM ORC JIT
  --build-dir <dir>    CMake build dir (default: Compiler/build)
  -j, --jobs <n>       Parallel build jobs (default: $JOBS)
  -h, --help           Show this help

Environment:
  BUILD_DIR=/path
  CONFIG=Release|Debug
  JOBS=2
  BINDIR=$HOME/.local/bin
  SIMPLEVM_ENABLE_LLVM_JIT=ON|OFF
  LLVM_DIR=/usr/lib/llvm-22/lib/cmake/llvm
  CC=clang-22 CXX=clang++-22
EOF
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --llvm-jit|--llvm)
      LLVM_JIT=ON
      shift
      ;;
    --no-llvm-jit|--no-llvm)
      LLVM_JIT=OFF
      shift
      ;;
    --build-dir)
      BUILD_DIR="$2"
      shift 2
      ;;
    -j|--jobs)
      JOBS="$2"
      shift 2
      ;;
    -h|--help)
      usage
      exit 0
      ;;
    *)
      echo "unknown option: $1" >&2
      usage >&2
      exit 1
      ;;
  esac
done

case "$LLVM_JIT" in
  1|true|TRUE|on|ON|yes|YES) LLVM_JIT=ON ;;
  *) LLVM_JIT=OFF ;;
esac

configure_args=(
  -S "$ROOT_DIR"
  -B "$BUILD_DIR"
  -DCMAKE_BUILD_TYPE="$CONFIG"
  -DSIMPLEVM_ENABLE_LLVM_JIT="$LLVM_JIT"
)

if [[ "$LLVM_JIT" == "ON" ]]; then
  if [[ -z "${LLVM_DIR:-}" && -d /usr/lib/llvm-22/lib/cmake/llvm ]]; then
    export LLVM_DIR=/usr/lib/llvm-22/lib/cmake/llvm
  fi
  if [[ -n "${LLVM_DIR:-}" ]]; then
    configure_args+=(-DLLVM_DIR="$LLVM_DIR")
  fi
  if [[ -z "${CC:-}" ]] && command -v clang-22 >/dev/null 2>&1; then
    export CC=clang-22
  fi
  if [[ -z "${CXX:-}" ]] && command -v clang++-22 >/dev/null 2>&1; then
    export CXX=clang++-22
  fi
  if [[ -n "${CC:-}" ]]; then
    configure_args+=(-DCMAKE_C_COMPILER="$CC")
  fi
  if [[ -n "${CXX:-}" ]]; then
    configure_args+=(-DCMAKE_CXX_COMPILER="$CXX")
  fi
fi

echo "Building Simple VM"
echo "  source:   $ROOT_DIR"
echo "  build:    $BUILD_DIR"
echo "  install:  $BINDIR"
echo "  llvm-jit: $LLVM_JIT"

cmake "${configure_args[@]}"
cmake --build "$BUILD_DIR" --target simplevm simple_stub --parallel "$JOBS"

mkdir -p "$ROOT_DIR/bin"
cp "$BUILD_DIR/bin/svm" "$ROOT_DIR/bin/svm"
cp "$BUILD_DIR/bin/simple" "$ROOT_DIR/bin/simple"
chmod +x "$ROOT_DIR/bin/svm" "$ROOT_DIR/bin/simple"

mkdir -p "$BINDIR"
install -m 0755 "$ROOT_DIR/bin/svm" "$BINDIR/svm"
install -m 0755 "$ROOT_DIR/bin/simple" "$BINDIR/simple"

cat <<EOF

Installed:
  $BINDIR/svm
  $BINDIR/simple

Source-tree copies:
  $ROOT_DIR/bin/svm
  $ROOT_DIR/bin/simple
EOF

if [[ ":$PATH:" != *":$BINDIR:"* ]]; then
  cat <<EOF

$BINDIR is not currently on PATH.
Add this to ~/.bashrc if you want svm/simple globally:

  export PATH="$BINDIR:\$PATH"

Then run:

  source ~/.bashrc
EOF
else
  echo ""
  echo "$BINDIR is already on PATH; svm should be globally available."
fi

echo ""
"$BINDIR/svm" -v || true
