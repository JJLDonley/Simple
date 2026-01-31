#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
OUT_DIR="$ROOT_DIR/bin"
mkdir -p "$OUT_DIR"

# g++ -std=c++17 -O2 -Wall -Wextra -I"$ROOT_DIR/include" \
#   "$ROOT_DIR/src/heap.cpp" \
#   "$ROOT_DIR/src/ir_builder.cpp" \
#   "$ROOT_DIR/src/opcode.cpp" \
#   "$ROOT_DIR/src/sbc_loader.cpp" \
#   "$ROOT_DIR/src/sbc_verifier.cpp" \
#   "$ROOT_DIR/src/vm.cpp" \
#   "$ROOT_DIR/src/main.cpp" \
#   -o "$OUT_DIR/simplevm"

g++ -std=c++17 -O2 -Wall -Wextra -I"$ROOT_DIR/include" \
  "$ROOT_DIR/src/heap.cpp" \
  "$ROOT_DIR/src/ir_builder.cpp" \
  "$ROOT_DIR/src/opcode.cpp" \
  "$ROOT_DIR/src/sbc_loader.cpp" \
  "$ROOT_DIR/src/sbc_verifier.cpp" \
  "$ROOT_DIR/src/vm.cpp" \
  "$ROOT_DIR/tests/test_main.cpp" \
  -o "$OUT_DIR/simplevm_tests"

# echo "built: $OUT_DIR/simplevm"
echo "built: $OUT_DIR/simplevm_tests"

echo "running: $OUT_DIR/simplevm_tests"
"$OUT_DIR/simplevm_tests"
