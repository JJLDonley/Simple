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

set(SIMPLEVM_CORE_SRC
  src/heap.cpp
  src/opcode.cpp
  src/sbc_loader.cpp
  src/sbc_verifier.cpp
  src/vm.cpp
)

add_executable(simplevm
  src/main.cpp
  ${SIMPLEVM_CORE_SRC}
)

add_executable(simplevm_tests
  tests/test_main.cpp
  ${SIMPLEVM_CORE_SRC}
)

target_include_directories(simplevm PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_include_directories(simplevm_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)

if (MSVC)
  target_compile_options(simplevm PRIVATE /W4 /permissive-)
  target_compile_options(simplevm_tests PRIVATE /W4 /permissive-)
else()
  target_compile_options(simplevm PRIVATE -Wall -Wextra -Wpedantic)
  target_compile_options(simplevm_tests PRIVATE -Wall -Wextra -Wpedantic)
endif()

enable_testing()
add_test(NAME simplevm_tests COMMAND simplevm_tests)
