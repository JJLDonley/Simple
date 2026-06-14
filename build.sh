#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="$ROOT_DIR/build"
CONFIG="${CONFIG:-Release}"
JOBS="${JOBS:-2}"

cmake -S "$ROOT_DIR" -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE="$CONFIG"
cmake --build "$BUILD_DIR" --target simplevm_runtime_static --parallel "$JOBS"
cmake --build "$BUILD_DIR" --target simplevm_runtime_shared --parallel "$JOBS"
cmake --build "$BUILD_DIR" --target simplevm --parallel "$JOBS"
cmake --build "$BUILD_DIR" --target simple_stub --parallel "$JOBS"

rm -rf "$ROOT_DIR/bin"
mkdir -p "$ROOT_DIR/bin"
cp "$BUILD_DIR/bin/svm" "$ROOT_DIR/bin/svm"
cp "$BUILD_DIR/bin/simple" "$ROOT_DIR/bin/simple"
chmod +x "$ROOT_DIR/bin/svm" "$ROOT_DIR/bin/simple"

printf 'Built:\n  %s\n  %s\n' "$ROOT_DIR/bin/svm" "$ROOT_DIR/bin/simple"
