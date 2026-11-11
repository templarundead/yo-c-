cmake_minimum_required(VERSION 3.28)

project(yo-c- LANGUAGES CXX VERSION 1.0.0)

# ---------------------------------------------------------------------------
# Глобальные настройки
# ---------------------------------------------------------------------------
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
# Модули C++ не используем; сканирование зависимостей ломается на кросс-
# компиляторах без clang-scan-deps (zig cc, mingw) и только замедляет сборку.
set(CMAKE_CXX_SCAN_FOR_MODULES OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

option(YO_BUILD_GUI "Собрать GUI (только Windows)" OFF)
option(YO_BUILD_CLI "Собрать yo-cli" ON)
option(YO_BUILD_TESTS "Собрать тесты (Catch2)" ON)
option(YO_BUILD_BENCH "Собрать бенчмарки (Google Benchmark)" OFF)
option(YO_WITH_RE2 "Использовать RE2 для словаря регулярных выражений (обязательно)" ON)
option(YO_SANITIZERS "ASan + UBSan в Debug" OFF)
option(YO_WARNINGS_AS_ERRORS "Предупреждения как ошибки" ON)

if(NOT YO_WITH_RE2)
	message(FATAL_ERROR "RE2 обязателен (утверждено в ARCHITECTURE.md §12 п.1)")
endif()

if(YO_BUILD_GUI AND NOT WIN32)
	message(FATAL_ERROR "YO_BUILD_GUI=ON поддерживается только на Windows")
endif()

# ---------------------------------------------------------------------------
# Предупреждения
# ---------------------------------------------------------------------------
add_library(yo_warnings INTERFACE)
if(MSVC)
	target_compile_options(yo_warnings INTERFACE /W4 /permissive- /utf-8 /Zc:__cplusplus /EHsc)
	if(YO_WARNINGS_AS_ERRORS)
		target_compile_options(yo_warnings INTERFACE /WX)
	endif()
else()
	target_compile_options(yo_warnings INTERFACE
		-Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wsign-conversion
		-Wold-style-cast -Wnon-virtual-dtor -Woverloaded-virtual -Wnull-dereference)
	if(YO_WARNINGS_AS_ERRORS)
		target_compile_options(yo_warnings INTERFACE -Werror)
	endif()
endif()

if(YO_SANITIZERS)
	add_library(yo_sanitizers INTERFACE)
	if(MSVC)
		target_compile_options(yo_sanitizers INTERFACE /fsanitize=address)
		target_link_options(yo_sanitizers INTERFACE /fsanitize=address)
	else()
		target_compile_options(yo_sanitizers INTERFACE -fsanitize=address,undefined -fno-omit-frame-pointer)
		target_link_options(yo_sanitizers INTERFACE -fsanitize=address,undefined)
	endif()
else()
	add_library(yo_sanitizers INTERFACE)
endif()

include(cmake/Dependencies.cmake)

add_subdirectory(src/core)
if(YO_BUILD_CLI)
	add_subdirectory(src/cli)
endif()
if(YO_BUILD_TESTS)
	enable_testing()
	add_subdirectory(tests)
endif()
if(YO_BUILD_BENCH)
	add_subdirectory(bench)
endif()
if(YO_BUILD_GUI)
	add_subdirectory(src/app)
endif()
