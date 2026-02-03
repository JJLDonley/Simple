cmake_minimum_required(VERSION 3.16)
project(simplevm LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
foreach(cfg DEBUG RELEASE RELWITHDEBINFO MINSIZEREL)
  string(TOUPPER "${cfg}" cfg_upper)
  set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_${cfg_upper} ${CMAKE_BINARY_DIR}/bin)
endforeach()

set(SIMPLEVM_ROOT ${CMAKE_CURRENT_SOURCE_DIR})
set(SIMPLEVM_VM_ROOT ${SIMPLEVM_ROOT}/VM)
set(SIMPLEVM_IR_ROOT ${SIMPLEVM_ROOT}/IR)
set(SIMPLEVM_BYTE_ROOT ${SIMPLEVM_ROOT}/Byte)
set(SIMPLEVM_LANG_ROOT ${SIMPLEVM_ROOT}/Lang)
set(SIMPLEVM_TEST_ROOT ${SIMPLEVM_ROOT}/Tests/tests)
set(SIMPLEVM_CORE_SRC
  ${SIMPLEVM_VM_ROOT}/src/heap.cpp
  ${SIMPLEVM_VM_ROOT}/src/vm.cpp
  ${SIMPLEVM_IR_ROOT}/src/ir_builder.cpp
  ${SIMPLEVM_IR_ROOT}/src/ir_compiler.cpp
  ${SIMPLEVM_IR_ROOT}/src/ir_lang.cpp
  ${SIMPLEVM_LANG_ROOT}/src/lang_lexer.cpp
  ${SIMPLEVM_LANG_ROOT}/src/lang_parser.cpp
  ${SIMPLEVM_LANG_ROOT}/src/lang_validate.cpp
  ${SIMPLEVM_BYTE_ROOT}/src/opcode.cpp
  ${SIMPLEVM_BYTE_ROOT}/src/sbc_loader.cpp
  ${SIMPLEVM_BYTE_ROOT}/src/sbc_verifier.cpp
)

add_executable(simplevm
  ${SIMPLEVM_ROOT}/CLI/src/main.cpp
  ${SIMPLEVM_CORE_SRC}
)

add_executable(simplevm_tests
  ${SIMPLEVM_TEST_ROOT}/test_core.cpp
  ${SIMPLEVM_TEST_ROOT}/test_ir.cpp
  ${SIMPLEVM_TEST_ROOT}/test_jit.cpp
  ${SIMPLEVM_TEST_ROOT}/test_lang.cpp
  ${SIMPLEVM_TEST_ROOT}/test_main.cpp
  ${SIMPLEVM_TEST_ROOT}/sir_runner.cpp
  ${SIMPLEVM_TEST_ROOT}/test_utils.cpp
  ${SIMPLEVM_CORE_SRC}
)

target_include_directories(simplevm PRIVATE
  ${SIMPLEVM_VM_ROOT}/include
  ${SIMPLEVM_IR_ROOT}/include
  ${SIMPLEVM_LANG_ROOT}/include
  ${SIMPLEVM_BYTE_ROOT}/include
)
target_include_directories(simplevm_tests PRIVATE
  ${SIMPLEVM_VM_ROOT}/include
  ${SIMPLEVM_IR_ROOT}/include
  ${SIMPLEVM_LANG_ROOT}/include
  ${SIMPLEVM_BYTE_ROOT}/include
)

if (MSVC)
  target_compile_options(simplevm PRIVATE /W4 /permissive-)
  target_compile_options(simplevm_tests PRIVATE /W4 /permissive-)
else()
  target_compile_options(simplevm PRIVATE -Wall -Wextra -Wpedantic)
  target_compile_options(simplevm_tests PRIVATE -Wall -Wextra -Wpedantic)
endif()

enable_testing()
add_test(NAME simplevm_tests COMMAND simplevm_tests)
