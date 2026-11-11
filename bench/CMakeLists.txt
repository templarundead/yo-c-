add_executable(yo_bench bench_yo.cpp)
target_link_libraries(yo_bench PRIVATE yo::core benchmark::benchmark)
target_compile_definitions(yo_bench PRIVATE
	YO_BENCH_DATA_DIR="${CMAKE_SOURCE_DIR}/tests/data")
set_target_properties(yo_bench PROPERTIES CXX_STANDARD 23 CXX_STANDARD_REQUIRED ON)

# Макросы Google Benchmark используют __COUNTER__ — в свежих clang это
# «расширение C2y» и с -Werror превращается в ошибку. Только clang: GCC
# на незнакомый -Wno-error=... ругается сам.
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
	target_compile_options(yo_bench PRIVATE -Wno-error=c2y-extensions)
endif()
