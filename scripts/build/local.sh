#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
BUILD_DIR="$ROOT_DIR/build"
CONFIG="${CONFIG:-Release}"
JOBS="${JOBS:-2}"

CACHE_FILE="$BUILD_DIR/CMakeCache.txt"
if [[ -f "$CACHE_FILE" ]]; then
  cached_source="$(grep '^CMAKE_HOME_DIRECTORY:INTERNAL=' "$CACHE_FILE" 2>/dev/null | cut -d= -f2- || true)"
  if [[ -n "$cached_source" && "$cached_source" != "$ROOT_DIR" ]]; then
    echo "Removing stale CMake cache from previous source path: $cached_source"
    rm -rf "$BUILD_DIR"
  fi
fi

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
