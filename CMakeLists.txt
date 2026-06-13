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
  ${SIMPLEVM_VM_ROOT}/src/ffi/dl_runtime.cpp
  ${SIMPLEVM_VM_ROOT}/src/gc/root_tracer.cpp
  ${SIMPLEVM_VM_ROOT}/src/heap.cpp
  ${SIMPLEVM_VM_ROOT}/src/interpreter/interpreter.cpp
  ${SIMPLEVM_VM_ROOT}/src/interpreter/dispatch.cpp
  ${SIMPLEVM_VM_ROOT}/src/interpreter/frames.cpp
  ${SIMPLEVM_VM_ROOT}/src/interpreter/stack.cpp
  ${SIMPLEVM_VM_ROOT}/src/jit/jit_scaffold.cpp
  ${SIMPLEVM_VM_ROOT}/src/vm.cpp
  ${SIMPLEVM_VM_ROOT}/src/native/buffer.cpp
  ${SIMPLEVM_VM_ROOT}/src/native/channel.cpp
  ${SIMPLEVM_VM_ROOT}/src/native/env.cpp
  ${SIMPLEVM_VM_ROOT}/src/native/fs.cpp
  ${SIMPLEVM_VM_ROOT}/src/native/json.cpp
  ${SIMPLEVM_VM_ROOT}/src/native/log.cpp
  ${SIMPLEVM_VM_ROOT}/src/native/os.cpp
  ${SIMPLEVM_VM_ROOT}/src/native/path.cpp
  ${SIMPLEVM_VM_ROOT}/src/native/random.cpp
  ${SIMPLEVM_VM_ROOT}/src/native/registry.cpp
  ${SIMPLEVM_VM_ROOT}/src/native/thread.cpp
  ${SIMPLEVM_VM_ROOT}/src/native/time.cpp
  ${SIMPLEVM_VM_ROOT}/src/runtime/runtime_limits.cpp
  ${SIMPLEVM_BYTE_ROOT}/src/opcode.cpp
  ${SIMPLEVM_BYTE_ROOT}/src/sbc_loader.cpp
  ${SIMPLEVM_BYTE_ROOT}/src/sbc_verifier.cpp
)
set(SIMPLEVM_FRONTEND_SRC
  ${SIMPLEVM_IR_ROOT}/src/ir_builder.cpp
  ${SIMPLEVM_IR_ROOT}/src/ir_compiler.cpp
  ${SIMPLEVM_IR_ROOT}/src/ir_lang.cpp
  ${SIMPLEVM_LANG_ROOT}/src/Lexer/lexer.cpp
  ${SIMPLEVM_LANG_ROOT}/src/CAST/parser.cpp
  ${SIMPLEVM_LANG_ROOT}/src/AST/lower_cast.cpp
  ${SIMPLEVM_LANG_ROOT}/src/Diagnostics/diagnostic.cpp
  ${SIMPLEVM_LANG_ROOT}/src/RAST/import_graph.cpp
  ${SIMPLEVM_LANG_ROOT}/src/RAST/member_resolution.cpp
  ${SIMPLEVM_LANG_ROOT}/src/RAST/reserved_resolution.cpp
  ${SIMPLEVM_LANG_ROOT}/src/RAST/symbol_table.cpp
  ${SIMPLEVM_LANG_ROOT}/src/RAST/resolver.cpp
  ${SIMPLEVM_LANG_ROOT}/src/TAST/type_checker.cpp
  ${SIMPLEVM_LANG_ROOT}/src/TAST/abi.cpp
  ${SIMPLEVM_LANG_ROOT}/src/TAST/calls.cpp
  ${SIMPLEVM_LANG_ROOT}/src/TAST/expressions.cpp
  ${SIMPLEVM_LANG_ROOT}/src/TAST/generics.cpp
  ${SIMPLEVM_LANG_ROOT}/src/TAST/literals.cpp
  ${SIMPLEVM_LANG_ROOT}/src/TAST/mutability.cpp
  ${SIMPLEVM_LANG_ROOT}/src/TAST/statements.cpp
  ${SIMPLEVM_LANG_ROOT}/src/TAST/control_flow.cpp
  ${SIMPLEVM_LANG_ROOT}/src/TAST/types.cpp
  ${SIMPLEVM_LANG_ROOT}/src/IRB/ir_builder.cpp
  ${SIMPLEVM_LANG_ROOT}/src/IRE/sir_emitter.cpp
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
  ${SIMPLEVM_ROOT}/CLI/src/command_contract.cpp
  ${SIMPLEVM_ROOT}/CLI/src/diagnostic_render.cpp
  ${SIMPLEVM_ROOT}/CLI/src/import_contract.cpp
  ${SIMPLEVM_LSP_ROOT}/src/diagnostic_bridge.cpp
  ${SIMPLEVM_LSP_ROOT}/src/lsp_server.cpp
)
target_include_directories(simplevm PRIVATE
  ${SIMPLEVM_ROOT}/CLI/include
  ${SIMPLEVM_LSP_ROOT}/include
)
target_link_libraries(simplevm PRIVATE simplevm_core_static)
target_compile_definitions(simplevm PRIVATE SIMPLEVM_PROJECT_ROOT="${SIMPLEVM_ROOT}")
add_custom_command(TARGET simplevm POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:simplevm> $<TARGET_FILE_DIR:simplevm>/simple
  COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:simplevm> $<TARGET_FILE_DIR:simplevm>/svm
)

add_executable(simplevm_tests
  ${SIMPLEVM_TEST_ROOT}/test_core.cpp
  ${SIMPLEVM_TEST_ROOT}/test_ir.cpp
  ${SIMPLEVM_TEST_ROOT}/test_jit.cpp
  ${SIMPLEVM_TEST_ROOT}/test_lang.cpp
  ${SIMPLEVM_TEST_ROOT}/test_lsp.cpp
  ${SIMPLEVM_TEST_ROOT}/vm/test_gc.cpp
  ${SIMPLEVM_TEST_ROOT}/vm/test_heap.cpp
  ${SIMPLEVM_TEST_ROOT}/vm/test_interpreter.cpp
  ${SIMPLEVM_TEST_ROOT}/vm/test_jit.cpp
  ${SIMPLEVM_TEST_ROOT}/vm/test_native_channel.cpp
  ${SIMPLEVM_TEST_ROOT}/vm/test_native_fs.cpp
  ${SIMPLEVM_TEST_ROOT}/vm/test_runtime_limits.cpp
  ${SIMPLEVM_TEST_ROOT}/cli/test_cli_build.cpp
  ${SIMPLEVM_TEST_ROOT}/cli/test_cli_contract.cpp
  ${SIMPLEVM_TEST_ROOT}/cli/test_cli_diagnostics.cpp
  ${SIMPLEVM_TEST_ROOT}/cli/test_cli_imports.cpp
  ${SIMPLEVM_TEST_ROOT}/lang/test_ast.cpp
  ${SIMPLEVM_TEST_ROOT}/lang/test_cast.cpp
  ${SIMPLEVM_TEST_ROOT}/lang/test_integration.cpp
  ${SIMPLEVM_TEST_ROOT}/lang/test_irb.cpp
  ${SIMPLEVM_TEST_ROOT}/lang/test_ire.cpp
  ${SIMPLEVM_TEST_ROOT}/lang/test_lexer.cpp
  ${SIMPLEVM_TEST_ROOT}/lang/test_rast.cpp
  ${SIMPLEVM_TEST_ROOT}/lang/test_tast.cpp
  ${SIMPLEVM_TEST_ROOT}/test_main.cpp
  ${SIMPLEVM_TEST_ROOT}/sir_runner.cpp
  ${SIMPLEVM_TEST_ROOT}/simple_runner.cpp
  ${SIMPLEVM_TEST_ROOT}/test_utils.cpp
  ${SIMPLEVM_ROOT}/CLI/src/command_contract.cpp
  ${SIMPLEVM_ROOT}/CLI/src/diagnostic_render.cpp
  ${SIMPLEVM_ROOT}/CLI/src/import_contract.cpp
  ${SIMPLEVM_LSP_ROOT}/src/diagnostic_bridge.cpp
)
target_include_directories(simplevm_tests PRIVATE
  ${SIMPLEVM_TEST_ROOT}
  ${SIMPLEVM_ROOT}/CLI/include
  ${SIMPLEVM_LSP_ROOT}/include
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
