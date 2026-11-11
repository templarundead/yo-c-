# Кросс-сборка x86_64-windows-gnu драйвером zig: проверяет Windows-код GUI
# (Win32/D2D1/DirectWrite) на Linux-раннере, без Windows SDK.
# Поставочная сборка — clang-cl (cmake/toolchains/clang-cl.cmake).

set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR AMD64)

include("${CMAKE_CURRENT_LIST_DIR}/zig-clang.cmake")

set(CMAKE_CXX_FLAGS_INIT "-target x86_64-windows-gnu")
set(CMAKE_EXE_LINKER_FLAGS_INIT "-target x86_64-windows-gnu")

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
