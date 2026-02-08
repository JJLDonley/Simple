cmake_minimum_required(VERSION 3.16)
project(simplevm LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Resolve libffi once and link it from core/runtime targets so downstream
# executables (simplevm/tests) inherit the dependency transitively.
set(SIMPLEVM_NEEDS_FFI ON)
if (SIMPLEVM_NEEDS_FFI)
  find_package(PkgConfig QUIET)
  if (PkgConfig_FOUND)
    pkg_check_modules(FFI QUIET IMPORTED_TARGET libffi)
  endif()
  if (TARGET PkgConfig::FFI)
    set(SIMPLEVM_FFI_TARGET PkgConfig::FFI)
  else()
    find_library(SIMPLEVM_FFI_LIB NAMES ffi REQUIRED)
  endif()
endif()

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
foreach(cfg DEBUG RELEASE RELWITHDEBINFO MINSIZEREL)
  string(TOUPPER "${cfg}" cfg_upper)
  set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_${cfg_upper} ${CMAKE_BINARY_DIR}/bin)
  set(CMAKE_LIBRARY_OUTPUT_DIRECTORY_${cfg_upper} ${CMAKE_BINARY_DIR}/bin)
  set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY_${cfg_upper} ${CMAKE_BINARY_DIR}/bin)
endforeach()

set(SIMPLEVM_ROOT ${CMAKE_CURRENT_SOURCE_DIR})
get_filename_component(SIMPLEVM_WORKSPACE_ROOT ${SIMPLEVM_ROOT} DIRECTORY)
set(SIMPLEVM_VM_ROOT ${SIMPLEVM_ROOT}/VM)
set(SIMPLEVM_IR_ROOT ${SIMPLEVM_ROOT}/IR)
set(SIMPLEVM_BYTE_ROOT ${SIMPLEVM_ROOT}/Byte)
set(SIMPLEVM_LANG_ROOT ${SIMPLEVM_ROOT}/Lang)
set(SIMPLEVM_LSP_ROOT ${SIMPLEVM_ROOT}/LSP)
set(SIMPLEVM_TEST_ROOT ${SIMPLEVM_ROOT}/Tests/tests)
set(SIMPLEVM_RUNTIME_SRC
  ${SIMPLEVM_VM_ROOT}/src/heap.cpp
  ${SIMPLEVM_VM_ROOT}/src/vm.cpp
  ${SIMPLEVM_BYTE_ROOT}/src/opcode.cpp
  ${SIMPLEVM_BYTE_ROOT}/src/sbc_loader.cpp
  ${SIMPLEVM_BYTE_ROOT}/src/sbc_verifier.cpp
)
set(SIMPLEVM_FRONTEND_SRC
  ${SIMPLEVM_IR_ROOT}/src/ir_builder.cpp
  ${SIMPLEVM_IR_ROOT}/src/ir_compiler.cpp
  ${SIMPLEVM_IR_ROOT}/src/ir_lang.cpp
  ${SIMPLEVM_LANG_ROOT}/src/lang_lexer.cpp
  ${SIMPLEVM_LANG_ROOT}/src/lang_parser.cpp
  ${SIMPLEVM_LANG_ROOT}/src/lang_validate.cpp
  ${SIMPLEVM_LANG_ROOT}/src/lang_sir.cpp
)
set(SIMPLEVM_CORE_SRC
  ${SIMPLEVM_RUNTIME_SRC}
  ${SIMPLEVM_FRONTEND_SRC}
)

add_library(simplevm_core_obj OBJECT
  ${SIMPLEVM_CORE_SRC}
)

target_include_directories(simplevm_core_obj PUBLIC
  ${SIMPLEVM_VM_ROOT}/include
  ${SIMPLEVM_IR_ROOT}/include
  ${SIMPLEVM_LANG_ROOT}/include
  ${SIMPLEVM_BYTE_ROOT}/include
)
set_target_properties(simplevm_core_obj PROPERTIES POSITION_INDEPENDENT_CODE ON)

add_library(simplevm_core_static STATIC $<TARGET_OBJECTS:simplevm_core_obj>)
set_target_properties(simplevm_core_static PROPERTIES OUTPUT_NAME simplevm_core)
target_include_directories(simplevm_core_static PUBLIC
  ${SIMPLEVM_VM_ROOT}/include
  ${SIMPLEVM_IR_ROOT}/include
  ${SIMPLEVM_LANG_ROOT}/include
  ${SIMPLEVM_BYTE_ROOT}/include
)
if (SIMPLEVM_NEEDS_FFI)
  if (TARGET PkgConfig::FFI)
    target_link_libraries(simplevm_core_static PUBLIC ${CMAKE_DL_LIBS} ${SIMPLEVM_FFI_TARGET})
  else()
    target_link_libraries(simplevm_core_static PUBLIC ${CMAKE_DL_LIBS} ${SIMPLEVM_FFI_LIB})
  endif()
else()
  target_link_libraries(simplevm_core_static PUBLIC ${CMAKE_DL_LIBS})
endif()

add_library(simplevm_core_shared SHARED $<TARGET_OBJECTS:simplevm_core_obj>)
set_target_properties(simplevm_core_shared PROPERTIES OUTPUT_NAME simplevm_core)
target_include_directories(simplevm_core_shared PUBLIC
  ${SIMPLEVM_VM_ROOT}/include
  ${SIMPLEVM_IR_ROOT}/include
  ${SIMPLEVM_LANG_ROOT}/include
  ${SIMPLEVM_BYTE_ROOT}/include
)
if (SIMPLEVM_NEEDS_FFI)
  if (TARGET PkgConfig::FFI)
    target_link_libraries(simplevm_core_shared PUBLIC ${CMAKE_DL_LIBS} ${SIMPLEVM_FFI_TARGET})
  else()
    target_link_libraries(simplevm_core_shared PUBLIC ${CMAKE_DL_LIBS} ${SIMPLEVM_FFI_LIB})
  endif()
else()
  target_link_libraries(simplevm_core_shared PUBLIC ${CMAKE_DL_LIBS})
endif()
target_compile_definitions(simplevm_core_shared PUBLIC SIMPLEVM_SHARED PRIVATE SIMPLEVM_BUILDING_DLL)

add_library(simplevm_runtime_static STATIC ${SIMPLEVM_RUNTIME_SRC})
set_target_properties(simplevm_runtime_static PROPERTIES OUTPUT_NAME simplevm_runtime)
target_include_directories(simplevm_runtime_static PUBLIC
  ${SIMPLEVM_VM_ROOT}/include
  ${SIMPLEVM_BYTE_ROOT}/include
)
if (SIMPLEVM_NEEDS_FFI)
  if (TARGET PkgConfig::FFI)
    target_link_libraries(simplevm_runtime_static PUBLIC ${CMAKE_DL_LIBS} ${SIMPLEVM_FFI_TARGET})
  else()
    target_link_libraries(simplevm_runtime_static PUBLIC ${CMAKE_DL_LIBS} ${SIMPLEVM_FFI_LIB})
  endif()
else()
  target_link_libraries(simplevm_runtime_static PUBLIC ${CMAKE_DL_LIBS})
endif()
set_target_properties(simplevm_runtime_static PROPERTIES POSITION_INDEPENDENT_CODE ON)

add_library(simplevm_runtime_shared SHARED ${SIMPLEVM_RUNTIME_SRC})
set_target_properties(simplevm_runtime_shared PROPERTIES OUTPUT_NAME simplevm_runtime)
target_include_directories(simplevm_runtime_shared PUBLIC
  ${SIMPLEVM_VM_ROOT}/include
  ${SIMPLEVM_BYTE_ROOT}/include
)
if (SIMPLEVM_NEEDS_FFI)
  if (TARGET PkgConfig::FFI)
    target_link_libraries(simplevm_runtime_shared PUBLIC ${CMAKE_DL_LIBS} ${SIMPLEVM_FFI_TARGET})
  else()
    target_link_libraries(simplevm_runtime_shared PUBLIC ${CMAKE_DL_LIBS} ${SIMPLEVM_FFI_LIB})
  endif()
else()
  target_link_libraries(simplevm_runtime_shared PUBLIC ${CMAKE_DL_LIBS})
endif()
target_compile_definitions(simplevm_runtime_shared PUBLIC SIMPLEVM_SHARED PRIVATE SIMPLEVM_BUILDING_DLL)

add_executable(simplevm
  ${SIMPLEVM_ROOT}/CLI/src/main.cpp
  ${SIMPLEVM_LSP_ROOT}/src/lsp_server.cpp
)
target_include_directories(simplevm PRIVATE
  ${SIMPLEVM_LSP_ROOT}/include
)
target_link_libraries(simplevm PRIVATE simplevm_core_static)
target_compile_definitions(simplevm PRIVATE SIMPLEVM_PROJECT_ROOT="${SIMPLEVM_ROOT}")

add_executable(simplevm_tests
  ${SIMPLEVM_TEST_ROOT}/test_core.cpp
  ${SIMPLEVM_TEST_ROOT}/test_ir.cpp
  ${SIMPLEVM_TEST_ROOT}/test_jit.cpp
  ${SIMPLEVM_TEST_ROOT}/test_lang.cpp
  ${SIMPLEVM_TEST_ROOT}/test_lsp.cpp
  ${SIMPLEVM_TEST_ROOT}/test_main.cpp
  ${SIMPLEVM_TEST_ROOT}/sir_runner.cpp
  ${SIMPLEVM_TEST_ROOT}/simple_runner.cpp
  ${SIMPLEVM_TEST_ROOT}/test_utils.cpp
)
target_link_libraries(simplevm_tests PRIVATE simplevm_core_static)

function(simplevm_apply_warnings target_name)
  if (MSVC)
    target_compile_options(${target_name} PRIVATE /W4 /permissive-)
  else()
    target_compile_options(${target_name} PRIVATE -Wall -Wextra -Wpedantic)
  endif()
endfunction()

simplevm_apply_warnings(simplevm_core_obj)
simplevm_apply_warnings(simplevm_runtime_static)
simplevm_apply_warnings(simplevm_runtime_shared)
simplevm_apply_warnings(simplevm)
simplevm_apply_warnings(simplevm_tests)

enable_testing()
add_test(NAME simplevm_tests COMMAND simplevm_tests)
set_tests_properties(simplevm_tests PROPERTIES WORKING_DIRECTORY ${SIMPLEVM_WORKSPACE_ROOT})
