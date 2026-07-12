cmake_minimum_required(VERSION 3.16)
project(simplevm LANGUAGES C CXX)

option(SIMPLEVM_ENABLE_LLVM_JIT "Build experimental LLVM ORC JIT backend" OFF)
if (SIMPLEVM_ENABLE_LLVM_JIT)
  find_package(LLVM REQUIRED CONFIG)
  llvm_map_components_to_libnames(SIMPLEVM_LLVM_LIBS
    Core
    ExecutionEngine
    OrcJIT
    Support
    Target
    native
  )
  separate_arguments(SIMPLEVM_LLVM_DEFINITIONS NATIVE_COMMAND "${LLVM_DEFINITIONS}")
  execute_process(
    COMMAND "${LLVM_TOOLS_BINARY_DIR}/llvm-config" --ldflags --libs core executionengine orcjit native support target --system-libs
    OUTPUT_VARIABLE SIMPLEVM_LLVM_EMBED_LINK_FLAGS
    OUTPUT_STRIP_TRAILING_WHITESPACE
  )
  string(REPLACE "\n" " " SIMPLEVM_LLVM_EMBED_LINK_FLAGS "${SIMPLEVM_LLVM_EMBED_LINK_FLAGS}")
endif()

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
set(SIMPLEVM_VERSION_OVERRIDE "" CACHE STRING "Deprecated: must match VERSION when set")
file(READ "${SIMPLEVM_ROOT}/VERSION" SIMPLEVM_VERSION)
string(STRIP "${SIMPLEVM_VERSION}" SIMPLEVM_VERSION)
if (SIMPLEVM_VERSION_OVERRIDE AND NOT SIMPLEVM_VERSION_OVERRIDE STREQUAL SIMPLEVM_VERSION)
  message(FATAL_ERROR "SIMPLEVM_VERSION_OVERRIDE (${SIMPLEVM_VERSION_OVERRIDE}) does not match VERSION (${SIMPLEVM_VERSION}); clear the CMake cache override")
endif()
if (NOT SIMPLEVM_VERSION MATCHES "^v[0-9]+\\.[0-9]+\\.[0-9]+$")
  message(FATAL_ERROR "VERSION must be vMAJOR.MINOR.PATCH, got '${SIMPLEVM_VERSION}'")
endif()
set(SIMPLEVM_VM_ROOT ${SIMPLEVM_ROOT}/VM)
set(SIMPLEVM_IR_ROOT ${SIMPLEVM_ROOT}/IR)
set(SIMPLEVM_BYTE_ROOT ${SIMPLEVM_ROOT}/Byte)
set(SIMPLEVM_LANG_ROOT ${SIMPLEVM_ROOT}/Lang)
set(SIMPLEVM_LIBRARY_ROOT ${SIMPLEVM_ROOT}/Library)
set(SIMPLEVM_LSP_ROOT ${SIMPLEVM_ROOT}/LSP)
set(SIMPLEVM_CLI_ROOT ${SIMPLEVM_ROOT}/CLI)
set(SIMPLEVM_TEST_ROOT ${SIMPLEVM_ROOT}/Tests/tests)
set(SIMPLEVM_RUNTIME_SRC
  ${SIMPLEVM_LIBRARY_ROOT}/src/library_catalog.cpp
  ${SIMPLEVM_LIBRARY_ROOT}/src/library_legacy.cpp
  ${SIMPLEVM_VM_ROOT}/src/ffi/dl_call.cpp
  ${SIMPLEVM_VM_ROOT}/src/ffi/dl_runtime.cpp
  ${SIMPLEVM_VM_ROOT}/src/gc/root_tracer.cpp
  ${SIMPLEVM_VM_ROOT}/src/gc/stack_map_collection.cpp
  ${SIMPLEVM_VM_ROOT}/src/heap.cpp
  ${SIMPLEVM_VM_ROOT}/src/interpreter/interpreter.cpp
  ${SIMPLEVM_VM_ROOT}/src/interpreter/dispatch.cpp
  ${SIMPLEVM_VM_ROOT}/src/interpreter/frames.cpp
  ${SIMPLEVM_VM_ROOT}/src/interpreter/globals.cpp
  ${SIMPLEVM_VM_ROOT}/src/interpreter/stack.cpp
  ${SIMPLEVM_VM_ROOT}/src/interpreter/traps.cpp
  ${SIMPLEVM_VM_ROOT}/src/jit/call_context.cpp
  ${SIMPLEVM_VM_ROOT}/src/jit/jit_scaffold.cpp
  ${SIMPLEVM_VM_ROOT}/src/jit/llvm_backend.cpp
  ${SIMPLEVM_VM_ROOT}/src/jit/status.cpp
  ${SIMPLEVM_VM_ROOT}/src/vm.cpp
  ${SIMPLEVM_VM_ROOT}/src/native/arg_utils.cpp
  ${SIMPLEVM_VM_ROOT}/src/native/buffer.cpp
  ${SIMPLEVM_VM_ROOT}/src/native/call_context.cpp
  ${SIMPLEVM_VM_ROOT}/src/native/capability_policy.cpp
  ${SIMPLEVM_VM_ROOT}/src/native/channel.cpp
  ${SIMPLEVM_VM_ROOT}/src/native/default_registry.cpp
  ${SIMPLEVM_VM_ROOT}/src/native/dispatch.cpp
  ${SIMPLEVM_VM_ROOT}/src/native/env.cpp
  ${SIMPLEVM_VM_ROOT}/src/native/fs.cpp
  ${SIMPLEVM_VM_ROOT}/src/native/json.cpp
  ${SIMPLEVM_VM_ROOT}/src/native/log.cpp
  ${SIMPLEVM_VM_ROOT}/src/native/metadata.cpp
  ${SIMPLEVM_VM_ROOT}/src/native/os.cpp
  ${SIMPLEVM_VM_ROOT}/src/native/path.cpp
  ${SIMPLEVM_VM_ROOT}/src/native/random.cpp
  ${SIMPLEVM_VM_ROOT}/src/native/registry.cpp
  ${SIMPLEVM_VM_ROOT}/src/native/registry_core.cpp
  ${SIMPLEVM_VM_ROOT}/src/native/resource_registry.cpp
  ${SIMPLEVM_VM_ROOT}/src/native/spec_builder.cpp
  ${SIMPLEVM_VM_ROOT}/src/native/system_channel.cpp
  ${SIMPLEVM_VM_ROOT}/src/native/system_os.cpp
  ${SIMPLEVM_VM_ROOT}/src/native/system_random.cpp
  ${SIMPLEVM_VM_ROOT}/src/native/thread.cpp
  ${SIMPLEVM_VM_ROOT}/src/native/time.cpp
  ${SIMPLEVM_VM_ROOT}/src/runtime/abi.cpp
  ${SIMPLEVM_VM_ROOT}/src/runtime/execution_stats.cpp
  ${SIMPLEVM_VM_ROOT}/src/runtime/import_dispatch.cpp
  ${SIMPLEVM_VM_ROOT}/src/runtime/print_any.cpp
  ${SIMPLEVM_VM_ROOT}/src/runtime/promise.cpp
  ${SIMPLEVM_VM_ROOT}/src/runtime/runtime_limits.cpp
  ${SIMPLEVM_VM_ROOT}/src/runtime/type_identity.cpp
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
  ${SIMPLEVM_LANG_ROOT}/src/GEN/specializer.cpp
  ${SIMPLEVM_LANG_ROOT}/src/RAST/import_graph.cpp
  ${SIMPLEVM_LANG_ROOT}/src/RAST/import_index.cpp
  ${SIMPLEVM_LANG_ROOT}/src/RAST/import_loader.cpp
  ${SIMPLEVM_LANG_ROOT}/src/RAST/import_paths.cpp
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
  ${SIMPLEVM_LIBRARY_ROOT}/include
  ${SIMPLEVM_BYTE_ROOT}/include
)
set_target_properties(simplevm_core_obj PROPERTIES POSITION_INDEPENDENT_CODE ON)
if (SIMPLEVM_ENABLE_LLVM_JIT)
  target_include_directories(simplevm_core_obj SYSTEM PRIVATE ${LLVM_INCLUDE_DIRS})
  target_compile_definitions(simplevm_core_obj PRIVATE SIMPLEVM_HAS_LLVM)
  target_compile_options(simplevm_core_obj PRIVATE ${SIMPLEVM_LLVM_DEFINITIONS})
endif()

add_library(simplevm_core_static STATIC $<TARGET_OBJECTS:simplevm_core_obj>)
set_target_properties(simplevm_core_static PROPERTIES OUTPUT_NAME simplevm_core)
target_include_directories(simplevm_core_static PUBLIC
  ${SIMPLEVM_VM_ROOT}/include
  ${SIMPLEVM_IR_ROOT}/include
  ${SIMPLEVM_LANG_ROOT}/include
  ${SIMPLEVM_LIBRARY_ROOT}/include
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
if (SIMPLEVM_ENABLE_LLVM_JIT)
  target_link_libraries(simplevm_core_static PUBLIC ${SIMPLEVM_LLVM_LIBS})
endif()

add_library(simplevm_core_shared SHARED $<TARGET_OBJECTS:simplevm_core_obj>)
set_target_properties(simplevm_core_shared PROPERTIES OUTPUT_NAME simplevm_core)
target_include_directories(simplevm_core_shared PUBLIC
  ${SIMPLEVM_VM_ROOT}/include
  ${SIMPLEVM_IR_ROOT}/include
  ${SIMPLEVM_LANG_ROOT}/include
  ${SIMPLEVM_LIBRARY_ROOT}/include
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
if (SIMPLEVM_ENABLE_LLVM_JIT)
  target_link_libraries(simplevm_core_shared PUBLIC ${SIMPLEVM_LLVM_LIBS})
endif()
target_compile_definitions(simplevm_core_shared PUBLIC SIMPLEVM_SHARED PRIVATE SIMPLEVM_BUILDING_DLL)

add_library(simplevm_runtime_static STATIC ${SIMPLEVM_RUNTIME_SRC})
set_target_properties(simplevm_runtime_static PROPERTIES OUTPUT_NAME simplevm_runtime)
target_include_directories(simplevm_runtime_static PUBLIC
  ${SIMPLEVM_VM_ROOT}/include
  ${SIMPLEVM_LIBRARY_ROOT}/include
  ${SIMPLEVM_BYTE_ROOT}/include
)
if (SIMPLEVM_ENABLE_LLVM_JIT)
  target_include_directories(simplevm_runtime_static SYSTEM PRIVATE ${LLVM_INCLUDE_DIRS})
  target_compile_definitions(simplevm_runtime_static PRIVATE SIMPLEVM_HAS_LLVM)
  target_compile_options(simplevm_runtime_static PRIVATE ${SIMPLEVM_LLVM_DEFINITIONS})
endif()
if (SIMPLEVM_NEEDS_FFI)
  if (TARGET PkgConfig::FFI)
    target_link_libraries(simplevm_runtime_static PUBLIC ${CMAKE_DL_LIBS} ${SIMPLEVM_FFI_TARGET})
  else()
    target_link_libraries(simplevm_runtime_static PUBLIC ${CMAKE_DL_LIBS} ${SIMPLEVM_FFI_LIB})
  endif()
else()
  target_link_libraries(simplevm_runtime_static PUBLIC ${CMAKE_DL_LIBS})
endif()
if (SIMPLEVM_ENABLE_LLVM_JIT)
  target_link_libraries(simplevm_runtime_static PUBLIC ${SIMPLEVM_LLVM_LIBS})
endif()
set_target_properties(simplevm_runtime_static PROPERTIES POSITION_INDEPENDENT_CODE ON)

add_library(simplevm_runtime_shared SHARED ${SIMPLEVM_RUNTIME_SRC})
set_target_properties(simplevm_runtime_shared PROPERTIES OUTPUT_NAME simplevm_runtime)
target_include_directories(simplevm_runtime_shared PUBLIC
  ${SIMPLEVM_VM_ROOT}/include
  ${SIMPLEVM_LIBRARY_ROOT}/include
  ${SIMPLEVM_BYTE_ROOT}/include
)
if (SIMPLEVM_ENABLE_LLVM_JIT)
  target_include_directories(simplevm_runtime_shared SYSTEM PRIVATE ${LLVM_INCLUDE_DIRS})
  target_compile_definitions(simplevm_runtime_shared PRIVATE SIMPLEVM_HAS_LLVM)
  target_compile_options(simplevm_runtime_shared PRIVATE ${SIMPLEVM_LLVM_DEFINITIONS})
endif()
if (SIMPLEVM_NEEDS_FFI)
  if (TARGET PkgConfig::FFI)
    target_link_libraries(simplevm_runtime_shared PUBLIC ${CMAKE_DL_LIBS} ${SIMPLEVM_FFI_TARGET})
  else()
    target_link_libraries(simplevm_runtime_shared PUBLIC ${CMAKE_DL_LIBS} ${SIMPLEVM_FFI_LIB})
  endif()
else()
  target_link_libraries(simplevm_runtime_shared PUBLIC ${CMAKE_DL_LIBS})
endif()
if (SIMPLEVM_ENABLE_LLVM_JIT)
  target_link_libraries(simplevm_runtime_shared PUBLIC ${SIMPLEVM_LLVM_LIBS})
endif()
target_compile_definitions(simplevm_runtime_shared PUBLIC SIMPLEVM_SHARED PRIVATE SIMPLEVM_BUILDING_DLL)

add_executable(simplevm
  ${SIMPLEVM_ROOT}/CLI/src/main.cpp
  ${SIMPLEVM_ROOT}/CLI/src/build_contract.cpp
  ${SIMPLEVM_ROOT}/CLI/src/command_contract.cpp
  ${SIMPLEVM_ROOT}/CLI/src/command_dispatch.cpp
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
target_compile_definitions(simplevm PRIVATE
  SIMPLEVM_PROJECT_ROOT="${SIMPLEVM_ROOT}"
  SIMPLEVM_VERSION="${SIMPLEVM_VERSION}"
)
if (SIMPLEVM_ENABLE_LLVM_JIT)
  target_compile_definitions(simplevm PRIVATE SIMPLEVM_EMBED_LLVM_LINK_FLAGS="${SIMPLEVM_LLVM_EMBED_LINK_FLAGS}")
endif()
set_target_properties(simplevm PROPERTIES OUTPUT_NAME svm)
add_custom_command(TARGET simplevm POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E rm -f $<TARGET_FILE_DIR:simplevm>/simplevm
)

add_executable(simple_stub
  ${SIMPLEVM_CLI_ROOT}/src/simple_stub.cpp
)
set_target_properties(simple_stub PROPERTIES OUTPUT_NAME simple)
target_include_directories(simple_stub PRIVATE
  ${SIMPLEVM_VM_ROOT}/include
  ${SIMPLEVM_BYTE_ROOT}/include
)
target_link_libraries(simple_stub PRIVATE simplevm_core_static)

add_executable(simplevm_tests
  ${SIMPLEVM_TEST_ROOT}/test_audit.cpp
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
  ${SIMPLEVM_TEST_ROOT}/vm/test_runtime_abi.cpp
  ${SIMPLEVM_TEST_ROOT}/vm/test_runtime_limits.cpp
  ${SIMPLEVM_TEST_ROOT}/cli/cli_test_utils.cpp
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
  ${SIMPLEVM_ROOT}/CLI/src/build_contract.cpp
  ${SIMPLEVM_ROOT}/CLI/src/command_contract.cpp
  ${SIMPLEVM_ROOT}/CLI/src/command_dispatch.cpp
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
if (SIMPLEVM_ENABLE_LLVM_JIT)
  target_compile_definitions(simplevm_tests PRIVATE SIMPLEVM_EMBED_LLVM_LINK_FLAGS="${SIMPLEVM_LLVM_EMBED_LINK_FLAGS}")
endif()

install(TARGETS simplevm simple_stub simplevm_core_static simplevm_core_shared simplevm_runtime_static simplevm_runtime_shared
  RUNTIME DESTINATION bin
  LIBRARY DESTINATION lib
  ARCHIVE DESTINATION lib
)
install(DIRECTORY ${SIMPLEVM_VM_ROOT}/include/ DESTINATION include/simple/VM)
install(DIRECTORY ${SIMPLEVM_BYTE_ROOT}/include/ DESTINATION include/simple/Byte)
install(DIRECTORY ${SIMPLEVM_IR_ROOT}/include/ DESTINATION include/simple/IR)
install(DIRECTORY ${SIMPLEVM_LANG_ROOT}/include/ DESTINATION include/simple/Lang)
install(DIRECTORY ${SIMPLEVM_LIBRARY_ROOT}/include/ DESTINATION include/simple/Library)
install(FILES ${SIMPLEVM_ROOT}/VERSION DESTINATION share/simple)

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
simplevm_apply_warnings(simple_stub)
simplevm_apply_warnings(simplevm_tests)

enable_testing()
add_test(NAME simplevm_tests COMMAND simplevm_tests)
set_tests_properties(simplevm_tests PROPERTIES WORKING_DIRECTORY ${SIMPLEVM_ROOT})
