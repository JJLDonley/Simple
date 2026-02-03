#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
OUT_DIR="$ROOT_DIR/bin"
BUILD_DIR="$ROOT_DIR/build"
OBJ_DIR="$BUILD_DIR/obj"
mkdir -p "$OUT_DIR"
mkdir -p "$OBJ_DIR"

# g++ -std=c++17 -O2 -Wall -Wextra -I"$ROOT_DIR/include" \
#   "$ROOT_DIR/src/heap.cpp" \
#   "$ROOT_DIR/src/ir_builder.cpp" \
#   "$ROOT_DIR/src/ir_compiler.cpp" \
#   "$ROOT_DIR/src/opcode.cpp" \
#   "$ROOT_DIR/src/sbc_loader.cpp" \
#   "$ROOT_DIR/src/sbc_verifier.cpp" \
#   "$ROOT_DIR/src/vm.cpp" \
#   "$ROOT_DIR/src/main.cpp" \
#   -o "$OUT_DIR/simplevm"

SOURCES=(
  "$ROOT_DIR/src/heap.cpp"
  "$ROOT_DIR/src/ir_builder.cpp"
  "$ROOT_DIR/src/ir_compiler.cpp"
  "$ROOT_DIR/src/ir_lang.cpp"
  "$ROOT_DIR/src/opcode.cpp"
  "$ROOT_DIR/src/sbc_loader.cpp"
  "$ROOT_DIR/src/sbc_verifier.cpp"
  "$ROOT_DIR/src/vm.cpp"
  "$ROOT_DIR/tests/test_core.cpp"
  "$ROOT_DIR/tests/test_ir.cpp"
  "$ROOT_DIR/tests/test_jit.cpp"
  "$ROOT_DIR/tests/test_main.cpp"
  "$ROOT_DIR/tests/test_utils.cpp"
)

OBJECTS=()
for src in "${SOURCES[@]}"; do
  base="$(basename "$src" .cpp)"
  obj="$OBJ_DIR/$base.o"
  dep="$OBJ_DIR/$base.d"
  OBJECTS+=("$obj")
  rebuild=0
  if [[ ! -f "$obj" ]]; then
    rebuild=1
  elif [[ "$src" -nt "$obj" ]]; then
    rebuild=1
  elif [[ -f "$dep" ]]; then
    deps="$(sed -e 's/^[^:]*://;s/\\\\$//g' "$dep")"
    for d in $deps; do
      if [[ -f "$d" && "$d" -nt "$obj" ]]; then
        rebuild=1
        break
      fi
    done
  else
    rebuild=1
  fi

  if [[ "$rebuild" -eq 1 ]]; then
    g++ -std=c++17 -O2 -Wall -Wextra -I"$ROOT_DIR/include" -MMD -MP -c "$src" -o "$obj"
  fi
done

g++ -std=c++17 -O2 -Wall -Wextra -I"$ROOT_DIR/include" \
  "${OBJECTS[@]}" \
  -o "$OUT_DIR/simplevm_tests"

# echo "built: $OUT_DIR/simplevm"
echo "built: $OUT_DIR/simplevm_tests"

echo "running: $OUT_DIR/simplevm_tests"
"$OUT_DIR/simplevm_tests"
