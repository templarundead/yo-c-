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
	"Линковщик ELF-сборок: default, mold или lld (Linux; lld нужен clang+LTO)")
set_property(CACHE YO_LINKER PROPERTY STRINGS default mold lld)

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
	# Abseil выставляет CMAKE_MSVC_RUNTIME_LIBRARY в своей области видимости и
	# по умолчанию выбирает /MD: без этой переменной линковка падает с
	# LNK2038 / failifmismatch (RuntimeLibrary MD_DynamicRelease vs
	# MT_StaticRelease в наших объектниках).
	set(ABSL_MSVC_STATIC_RUNTIME ON CACHE BOOL "" FORCE)
	message(STATUS "CRT: статический (/MT)")
elseif(MSVC)
	set(ABSL_MSVC_STATIC_RUNTIME OFF CACHE BOOL "" FORCE)
	message(STATUS "CRT: динамический (/MD)")
endif()

# ---------------------------------------------------------------------------
# Линковщик: mold или lld — только ELF; для PE/MSVC неприменимо.
# lld нужен clang для LTO: биткодный объектный файл понимают только
# линковщики LLVM, GNU ld/bdf спотыкаются на нём, а плагин LLVM в комплект
# clang не входит. Поэтому lld ищется с точностью до версии компилятора:
# у Ubuntu ld.lld-20 ложится как /usr/bin/ld.lld-20 (или в /usr/lib/llvm-20).
# ---------------------------------------------------------------------------
if(NOT YO_LINKER STREQUAL "default")
	if(WIN32 OR MSVC)
		message(FATAL_ERROR
			"YO_LINKER=${YO_LINKER}: ELF-линковщик, для PE/MSVC неприменим")
	endif()
	if(YO_LINKER STREQUAL "mold")
		find_program(YO_LINKER_EXE NAMES mold)
		if(NOT YO_LINKER_EXE)
			message(FATAL_ERROR "YO_LINKER=mold, но mold не найден в PATH (apt install mold)")
		endif()
		add_link_options("-fuse-ld=mold")
	elseif(YO_LINKER STREQUAL "lld")
		string(REGEX MATCH "^[0-9]+" _yo_compiler_major "${CMAKE_CXX_COMPILER_VERSION}")
		# Версия компилятора в приоритете: /usr/bin/ld.lld из метапакета lld
		# может оказаться другой версии, а читать биткод новее себя он не умеет.
		find_program(YO_LINKER_EXE
			NAMES "ld.lld-${_yo_compiler_major}" ld.lld lld "lld-${_yo_compiler_major}")
		if(NOT YO_LINKER_EXE)
			# ld.lld из llvm.sh не попадает в PATH: он лежит рядом с clang.
			file(GLOB _yo_lld_candidates
				"/usr/lib/llvm-*/bin/ld.lld" "/usr/bin/ld.lld-*")
			list(SORT _yo_lld_candidates)
			if(_yo_lld_candidates)
				list(GET _yo_lld_candidates -1 YO_LINKER_EXE)
			endif()
		endif()
		if(NOT YO_LINKER_EXE)
			message(FATAL_ERROR
				"YO_LINKER=lld, но ld.lld не найден (apt install lld-${_yo_compiler_major})")
		endif()
		# clang понимает -fuse-ld=<абсолютный путь>, gcc — только имя.
		if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
			add_link_options("-fuse-ld=${YO_LINKER_EXE}")
		else()
			add_link_options("-fuse-ld=lld")
		endif()
	else()
		message(FATAL_ERROR
			"YO_LINKER=${YO_LINKER}: поддерживается default, mold или lld")
	endif()
	message(STATUS "Линковщик: ${YO_LINKER} (${YO_LINKER_EXE})")
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
			# clang на ELF отдаёт в линковщик биткод: ни GNU ld, ни bfd его не
			# понимают, а плагин LLVM в пакет clang не входит — нужен lld.
			if(CMAKE_CXX_COMPILER_ID MATCHES "Clang" AND NOT WIN32
					AND YO_LINKER STREQUAL "default")
				message(WARNING
					"YO_LTO=ON с clang требует lld: передайте -DYO_LINKER=lld")
			endif()
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
