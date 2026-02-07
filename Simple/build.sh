#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
VM_DIR="$ROOT_DIR/VM"
IR_DIR="$ROOT_DIR/IR"
BYTE_DIR="$ROOT_DIR/Byte"
CLI_DIR="$ROOT_DIR/CLI"
TEST_DIR="$ROOT_DIR/Tests/tests"
OUT_DIR="$ROOT_DIR/bin"
BUILD_DIR="$ROOT_DIR/build"
CORE_OBJ_DIR="$BUILD_DIR/obj_core"

SUITE="all"
if [[ "${1:-}" == "--suite" && -n "${2:-}" ]]; then
  SUITE="$2"
  shift 2
fi

case "$SUITE" in
  all|core|ir|jit|lang) ;;
  *)
    echo "unknown suite: $SUITE (expected: all|core|ir|jit|lang)" >&2
    exit 1
    ;;
esac

OBJ_DIR="$BUILD_DIR/obj_$SUITE"
mkdir -p "$OUT_DIR"
mkdir -p "$OBJ_DIR"
mkdir -p "$CORE_OBJ_DIR"

FFI_C="$ROOT_DIR/Tests/ffi/simple_ffi.c"
FFI_SO="$ROOT_DIR/Tests/ffi/libsimpleffi.so"
if [[ -f "$FFI_C" ]]; then
  if [[ ! -f "$FFI_SO" || "$FFI_C" -nt "$FFI_SO" ]]; then
    cc -shared -fPIC -O2 -o "$FFI_SO" "$FFI_C"
  fi
fi

CLI_SOURCES=(
  "$CLI_DIR/src/main.cpp"
)

RUNTIME_SOURCES=(
  "$VM_DIR/src/heap.cpp"
  "$VM_DIR/src/vm.cpp"
  "$BYTE_DIR/src/opcode.cpp"
  "$BYTE_DIR/src/sbc_loader.cpp"
  "$BYTE_DIR/src/sbc_verifier.cpp"
)

SOURCES=(
  "$IR_DIR/src/ir_builder.cpp"
  "$IR_DIR/src/ir_compiler.cpp"
  "$IR_DIR/src/ir_lang.cpp"
  "$ROOT_DIR/Lang/src/lang_lexer.cpp"
  "$ROOT_DIR/Lang/src/lang_parser.cpp"
  "$ROOT_DIR/Lang/src/lang_validate.cpp"
  "$ROOT_DIR/Lang/src/lang_sir.cpp"
  "$BYTE_DIR/src/opcode.cpp"
  "$BYTE_DIR/src/sbc_loader.cpp"
  "$BYTE_DIR/src/sbc_verifier.cpp"
  "$TEST_DIR/test_main.cpp"
  "$TEST_DIR/test_utils.cpp"
  "$TEST_DIR/sir_runner.cpp"
  "$TEST_DIR/simple_runner.cpp"
  "$TEST_DIR/test_lang.cpp"
)

TEST_DEFINES=()
case "$SUITE" in
  all)
    SOURCES+=("$TEST_DIR/test_core.cpp")
    SOURCES+=("$TEST_DIR/test_ir.cpp")
    SOURCES+=("$TEST_DIR/test_jit.cpp")
    ;;
  core)
    TEST_DEFINES+=("-DTEST_SUITE_CORE")
    SOURCES+=("$TEST_DIR/test_core.cpp")
    ;;
  ir)
    TEST_DEFINES+=("-DTEST_SUITE_IR")
    SOURCES+=("$TEST_DIR/test_ir.cpp")
    ;;
  jit)
    TEST_DEFINES+=("-DTEST_SUITE_JIT")
    SOURCES+=("$TEST_DIR/test_jit.cpp")
    ;;
  lang)
    TEST_DEFINES+=("-DTEST_SUITE_LANG")
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
    g++ -std=c++17 -O2 -Wall -Wextra \
      -I"$VM_DIR/include" \
      -I"$IR_DIR/include" \
      -I"$ROOT_DIR/Lang/include" \
      -I"$BYTE_DIR/include" \
      "${TEST_DEFINES[@]}" -MMD -MP -c "$src" -o "$obj"
  fi
done

CORE_OBJECTS=()
for src in "${RUNTIME_SOURCES[@]}"; do
  base="$(basename "$src" .cpp)"
  obj="$CORE_OBJ_DIR/$base.o"
  dep="$CORE_OBJ_DIR/$base.d"
  CORE_OBJECTS+=("$obj")
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
    g++ -std=c++17 -O2 -Wall -Wextra -fPIC \
      -I"$VM_DIR/include" \
      -I"$BYTE_DIR/include" \
      -MMD -MP -c "$src" -o "$obj"
  fi
done

ar rcs "$OUT_DIR/libsimplevm_runtime.a" "${CORE_OBJECTS[@]}"
g++ -shared -fPIC -O2 -Wall -Wextra \
  "${CORE_OBJECTS[@]}" \
  -ldl \
  -o "$OUT_DIR/libsimplevm_runtime.so"

g++ -std=c++17 -O2 -Wall -Wextra \
  -I"$VM_DIR/include" \
  -I"$IR_DIR/include" \
  -I"$ROOT_DIR/Lang/include" \
  -I"$BYTE_DIR/include" \
  "${TEST_DEFINES[@]}" \
  "${OBJECTS[@]}" \
  "$OUT_DIR/libsimplevm_runtime.a" \
  -ldl \
  -o "$OUT_DIR/simplevm_tests_$SUITE"

g++ -std=c++17 -O2 -Wall -Wextra \
  -I"$VM_DIR/include" \
  -I"$IR_DIR/include" \
  -I"$ROOT_DIR/Lang/include" \
  -I"$BYTE_DIR/include" \
  "${CLI_SOURCES[@]}" \
  "$IR_DIR/src/ir_builder.cpp" \
  "$IR_DIR/src/ir_compiler.cpp" \
  "$IR_DIR/src/ir_lang.cpp" \
  "$ROOT_DIR/Lang/src/lang_lexer.cpp" \
  "$ROOT_DIR/Lang/src/lang_parser.cpp" \
  "$ROOT_DIR/Lang/src/lang_validate.cpp" \
  "$ROOT_DIR/Lang/src/lang_sir.cpp" \
  "$OUT_DIR/libsimplevm_runtime.a" \
  -DSIMPLEVM_PROJECT_ROOT=\"$ROOT_DIR\" \
  -ldl \
  -o "$OUT_DIR/simplevm"

cp "$OUT_DIR/simplevm" "$OUT_DIR/simple"

echo "built: $OUT_DIR/simplevm"
echo "built: $OUT_DIR/simple"
echo "built: $OUT_DIR/libsimplevm_runtime.a"
echo "built: $OUT_DIR/libsimplevm_runtime.so"
echo "built: $OUT_DIR/simplevm_tests_$SUITE"

echo "running: $OUT_DIR/simplevm_tests_$SUITE"
"$OUT_DIR/simplevm_tests_$SUITE"
