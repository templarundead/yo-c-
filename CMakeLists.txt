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
option(YO_STATIC_RUNTIME
	"Статический CRT у MSVC/clang-cl: exe без vcruntime140.dll/msvcp140.dll" ON)
option(YO_LTO "LTO/IPO в Release (недоступно драйверу zig, см. YO_NO_IPO)" OFF)
option(YO_STRIP "Стрипить бинарники в Release (-Wl,-s y GNU-подобных)" OFF)
set(YO_LINKER "default" CACHE STRING
	"Линковщик ELF-сборок: default или mold (Linux; mold должен быть в PATH)")
set_property(CACHE YO_LINKER PROPERTY STRINGS default mold)

if(NOT YO_WITH_RE2)
	message(FATAL_ERROR "RE2 обязателен (утверждено в ARCHITECTURE.md §12 п.1)")
endif()

if(YO_BUILD_GUI AND NOT WIN32)
	message(FATAL_ERROR "YO_BUILD_GUI=ON поддерживается только на Windows")
endif()

# ---------------------------------------------------------------------------
# Статический CRT (MSVC/clang-cl)
# ---------------------------------------------------------------------------
# Поставочные exe не должны требовать vcruntime140.dll / msvcp140.dll: с
# динамическим CRT (/MD по умолчанию у CMake) программа на «чистой» Windows
# не запускается без VC++ Redistributable. /MT вшивает CRT в бинарник.
# С ASan так нельзя — санитайзеру нужен динамический CRT, поэтому в
# санитайзер-сборках оставляем умолчание.
if(MSVC AND YO_STATIC_RUNTIME AND NOT YO_SANITIZERS)
	set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
	message(STATUS "CRT: статический (/MT)")
elseif(MSVC)
	message(STATUS "CRT: динамический (/MD)")
endif()

# ---------------------------------------------------------------------------
# Линковщик (mold) — только ELF; для PE/MSVC неприменим
# ---------------------------------------------------------------------------
if(NOT YO_LINKER STREQUAL "default")
	if(WIN32 OR MSVC)
		message(FATAL_ERROR "YO_LINKER=${YO_LINKER}: mold не умеет PE-файлы, доступен только ELF")
	endif()
	if(NOT YO_LINKER STREQUAL "mold")
		message(FATAL_ERROR "YO_LINKER=${YO_LINKER}: поддерживается только default или mold")
	endif()
	find_program(YO_MOLD_EXE NAMES mold)
	if(NOT YO_MOLD_EXE)
		message(FATAL_ERROR "YO_LINKER=mold, но mold не найден в PATH (apt install mold)")
	endif()
	message(STATUS "Линковщик: mold (${YO_MOLD_EXE})")
	add_link_options("-fuse-ld=mold")
endif()

# ---------------------------------------------------------------------------
# LTO/IPO и strip для релизных сборок
# ---------------------------------------------------------------------------
# IPO включается только: Release, без санитайзеров и только там, где его
# действительно поддерживает инструментарий. Драйвер zig LTO не тянет
# (проверено: lld-link падает на «undefined symbol: nanl» из zigc.lib) —
# его тулчейн выставляет YO_NO_IPO, и проверка не запускается вовсе.
set(YO_IPO_SUPPORTED OFF)
if(YO_LTO)
	if(YO_NO_IPO)
		# Драйвер zig: LTO не линкуется (см. cmake/toolchains/zig-clang.cmake).
		message(STATUS "LTO: пропущено — инструментарий не поддерживает IPO")
	elseif(YO_SANITIZERS)
		message(STATUS "LTO: пропущено — включены санитайзеры")
	elseif(NOT CMAKE_BUILD_TYPE STREQUAL "Release")
		message(STATUS "LTO: пропущено — только для Release (сейчас ${CMAKE_BUILD_TYPE})")
	else()
		include(CheckIPOSupported)
		check_ipo_supported(RESULT YO_IPO_SUPPORTED OUTPUT _yo_ipo_output LANGUAGES CXX)
		if(YO_IPO_SUPPORTED)
			message(STATUS "LTO: включено")
		else()
			message(WARNING "YO_LTO=ON, но LTO не поддержан инструментарием:\n${_yo_ipo_output}")
		endif()
	endif()
endif()

# Применить релизные оптимизации к цели. Вызывается для своих целей (ядро,
# CLI, GUI, тесты), но не для сторонних (abseil/re2): LTO поверх сотен чужих
# единиц трансляции резко замедляет сборку, а выигрыша почти не даёт.
function(yo_apply_release_tweaks target)
	if(YO_IPO_SUPPORTED)
		set_target_properties(${target} PROPERTIES INTERPROCEDURAL_OPTIMIZATION TRUE)
	endif()
	if(YO_STRIP AND NOT YO_SANITIZERS AND CMAKE_BUILD_TYPE STREQUAL "Release")
		if(MSVC)
			# У clang-cl/MSVC нет DWARF/PDB без /DEBUG — стрипить нечего;
			# /OPT:REF и /OPT:ICF link.exe включает в Release сам.
			return()
		endif()
		target_link_options(${target} PRIVATE "-Wl,-s")
	endif()
endfunction()

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
