#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
OUT_DIR="$ROOT_DIR/bin"
BUILD_DIR="$ROOT_DIR/build"

SUITE="all"
if [[ "${1:-}" == "--suite" && -n "${2:-}" ]]; then
  SUITE="$2"
  shift 2
fi

case "$SUITE" in
  all|core|ir|jit) ;;
  *)
    echo "unknown suite: $SUITE (expected: all|core|ir|jit)" >&2
    exit 1
    ;;
esac

OBJ_DIR="$BUILD_DIR/obj_$SUITE"
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
  "$ROOT_DIR/tests/test_main.cpp"
  "$ROOT_DIR/tests/test_utils.cpp"
)

TEST_DEFINES=()
case "$SUITE" in
  all)
    SOURCES+=("$ROOT_DIR/tests/test_core.cpp")
    SOURCES+=("$ROOT_DIR/tests/test_ir.cpp")
    SOURCES+=("$ROOT_DIR/tests/test_jit.cpp")
    ;;
  core)
    TEST_DEFINES+=("-DTEST_SUITE_CORE")
    SOURCES+=("$ROOT_DIR/tests/test_core.cpp")
    ;;
  ir)
    TEST_DEFINES+=("-DTEST_SUITE_IR")
    SOURCES+=("$ROOT_DIR/tests/test_ir.cpp")
    ;;
  jit)
    TEST_DEFINES+=("-DTEST_SUITE_JIT")
    SOURCES+=("$ROOT_DIR/tests/test_jit.cpp")
    ;;
esac

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
    g++ -std=c++17 -O2 -Wall -Wextra -I"$ROOT_DIR/include" "${TEST_DEFINES[@]}" -MMD -MP -c "$src" -o "$obj"
  fi
done

g++ -std=c++17 -O2 -Wall -Wextra -I"$ROOT_DIR/include" \
  "${TEST_DEFINES[@]}" \
  "${OBJECTS[@]}" \
  -o "$OUT_DIR/simplevm_tests_$SUITE"

# echo "built: $OUT_DIR/simplevm"
echo "built: $OUT_DIR/simplevm_tests_$SUITE"

echo "running: $OUT_DIR/simplevm_tests_$SUITE"
"$OUT_DIR/simplevm_tests_$SUITE"
