# Целевой тулчейн: clang-cl (LLVM), x64, Windows 10+.
# Требует установленного LLVM (clang-cl.exe) и Windows SDK.
#
# Использование (из "x64 Native Tools Command Prompt" или после vcvars64):
#   cmake --preset win-clang-debug
#
# Переменные: YO_LLVM_DIR (например C:/Program Files/LLVM), YO_SDK_VERSION.

set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR AMD64)

if(NOT DEFINED YO_LLVM_DIR)
	set(YO_LLVM_DIR "$ENV{ProgramFiles}/LLVM")
endif()

find_program(YO_CLANG_CL clang-cl HINTS "${YO_LLVM_DIR}/bin" REQUIRED)
find_program(YO_LLD_LINK lld-link HINTS "${YO_LLVM_DIR}/bin" REQUIRED)

set(CMAKE_C_COMPILER "${YO_CLANG_CL}")
set(CMAKE_CXX_COMPILER "${YO_CLANG_CL}")
set(CMAKE_CXX_COMPILER_FRONTEND_VARIANT "MSVC")
set(CMAKE_LINKER "${YO_LLD_LINK}")
set(CMAKE_CXX_FLAGS_INIT "-m64")
set(CMAKE_EXE_LINKER_FLAGS_INIT "-m64")
# Не ищем библиотеки хоста (Linux), только целевые.
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
