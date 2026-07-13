set(SIMPLEVM_PLATFORM_NAME "windows")
# Match the static vcpkg triplet and avoid mixing LIBCMT with the DLL CRT.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
set(SIMPLEVM_RUNTIME_SHARED_NAME "simplevm_runtime_shared.lib")
set(SIMPLEVM_RUNTIME_STATIC_NAME "simplevm_runtime.lib")
set(SIMPLEVM_PACKAGE_EXTENSION "zip")
set(SIMPLEVM_PLATFORM_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/source/Platform/src/windows/platform.cpp")
