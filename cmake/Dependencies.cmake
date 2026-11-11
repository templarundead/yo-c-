# Единый способ управления зависимостями: FetchContent, пины по тегу.
# Офлайн-сборка: передать -DFETCHCONTENT_SOURCE_DIR_<NAME>=<путь> или
# -DFETCHCONTENT_FULLY_DISCONNECTED=ON при наличии кэша.

include(FetchContent)
set(FETCHCONTENT_QUIET OFF)

if(YO_BUILD_TESTS)
	FetchContent_Declare(catch2
		GIT_REPOSITORY https://github.com/catchorg/Catch2.git
		GIT_TAG v3.7.1
		GIT_SHALLOW TRUE
		EXCLUDE_FROM_ALL)
	set(YO_CATCH2_TARGET Catch2::Catch2WithMain)
	FetchContent_MakeAvailable(catch2)
endif()

if(YO_BUILD_BENCH)
	FetchContent_Declare(googlebenchmark
		GIT_REPOSITORY https://github.com/google/benchmark.git
		GIT_TAG v1.9.1
		GIT_SHALLOW TRUE
		EXCLUDE_FROM_ALL)
	set(BENCHMARK_ENABLE_TESTING OFF CACHE BOOL "" FORCE)
	set(BENCHMARK_ENABLE_WERROR OFF CACHE BOOL "" FORCE)
	set(BENCHMARK_ENABLE_GTEST_TESTS OFF CACHE BOOL "" FORCE)
	FetchContent_MakeAvailable(googlebenchmark)
	include_directories(SYSTEM "${googlebenchmark_SOURCE_DIR}/include")
	# Google Benchmark 1.9.1 включает -pedantic-errors, а свежие clang
	# считают __COUNTER__ расширением C2y — гасим этот диагностик.
	# __COUNTER__ в заголовках Google Benchmark — «расширение C2y» только
	# для clang 21+; у GCC такого предупреждения нет (и нет флага).
	if(TARGET benchmark AND CMAKE_CXX_COMPILER_ID MATCHES "Clang")
		target_compile_options(benchmark PRIVATE -Wno-c2y-extensions)
	endif()
endif()

if(YO_WITH_RE2)
	# RE2 требует Abseil как подпроект (find_package(absl) иначе).
	FetchContent_Declare(abseil
		GIT_REPOSITORY https://github.com/abseil/abseil-cpp.git
		GIT_TAG 20250814.1
		GIT_SHALLOW TRUE
		EXCLUDE_FROM_ALL)
	set(ABSL_PROPAGATE_CXX_STD ON CACHE BOOL "" FORCE)
	set(ABSL_BUILD_TESTING OFF CACHE BOOL "" FORCE)
	set(ABSL_ENABLE_INSTALL ON CACHE BOOL "" FORCE)
	set(ABSL_USE_GOOGLETEST_HEAD OFF CACHE BOOL "" FORCE)
	FetchContent_MakeAvailable(abseil)

	FetchContent_Declare(re2
		GIT_REPOSITORY https://github.com/google/re2.git
		GIT_TAG 2025-11-05
		GIT_SHALLOW TRUE
		EXCLUDE_FROM_ALL)
	set(RE2_BUILD_TESTING OFF CACHE BOOL "" FORCE)
	FetchContent_MakeAvailable(re2)
	# Заголовки сторонних библиотек подключаются как системные: их
	# предупреждения не должны попадать под -Werror нашего кода.
	include_directories(SYSTEM "${re2_SOURCE_DIR}" "${abseil_SOURCE_DIR}")
endif()
