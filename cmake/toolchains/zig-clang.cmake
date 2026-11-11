# Тулчейн для локальной проверки в Linux-песочнице: clang из zig (zig c++).
# Не является поставочным: целевой тулчейн — clang-cl (cmake/toolchains/clang-cl.cmake).
#
# Обёртки для ar/ranlib создаются в каталоге сборки, потому что zig-драйвер
# не принимает аргументы llvm-ar напрямую.

find_program(YO_ZIG_DRIVER NAMES zig REQUIRED)

set(CMAKE_CXX_COMPILER "${YO_ZIG_DRIVER}")
set(CMAKE_CXX_COMPILER_ARG1 "c++")
set(CMAKE_CXX_COMPILER_FRONTEND_VARIANT "GNU")
set(CMAKE_CXX_COMPILER_ID_RUN TRUE)
set(CMAKE_CXX_COMPILER_ID "Clang")
set(CMAKE_CXX_COMPILER_VERSION "21.0.0")

set(_yo_tools "${CMAKE_BINARY_DIR}/zig-tools")
file(MAKE_DIRECTORY "${_yo_tools}")
file(WRITE "${_yo_tools}/zig-ar.sh"
	"#!/bin/sh\nexec \"${YO_ZIG_DRIVER}\" ar \"$@\"\n")
file(WRITE "${_yo_tools}/zig-ranlib.sh"
	"#!/bin/sh\nexec \"${YO_ZIG_DRIVER}\" ranlib \"$@\"\n")
file(CHMOD "${_yo_tools}/zig-ar.sh" PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE)
file(CHMOD "${_yo_tools}/zig-ranlib.sh" PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE)

set(CMAKE_AR "${_yo_tools}/zig-ar.sh")
set(CMAKE_RANLIB "${_yo_tools}/zig-ranlib.sh")

# CMake не умеет определять стандарт по умолчанию для драйвера zig c++.
set(CMAKE_CXX_STANDARD_COMPUTED_DEFAULT 23 CACHE STRING "" FORCE)
set(CMAKE_CXX_EXTENSIONS_COMPUTED_DEFAULT OFF CACHE STRING "" FORCE)
set(CMAKE_CXX_SCAN_FOR_MODULES OFF CACHE BOOL "" FORCE)
