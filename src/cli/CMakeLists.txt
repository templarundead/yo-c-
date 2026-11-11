# yo-cli использует std::print/std::println (<print>, C++23). Библиотека
# стандарта с этим заголовком: libstdc++ >= 14, libc++ (zig, clang с
# -stdlib=libc++) >= 18, MSVC STL 17.10+. На Debian 12 штатный libstdc++ 12
# заголовка не имеет — clang там молча соберёт ядро и тесты, а CLI «исчезнет».
include(CheckCXXSourceCompiles)
check_cxx_source_compiles("
#include <print>
int main() { std::println(\"ok\"); }
" YO_HAS_PRINT)

if(YO_HAS_PRINT)
	add_executable(yo-cli main.cpp)
	target_link_libraries(yo-cli PRIVATE yo::core)
	set_target_properties(yo-cli PROPERTIES CXX_STANDARD 23 CXX_STANDARD_REQUIRED ON)
else()
	message(WARNING
		"yo-cli не будет собран: в стандартной библиотеке нет <print> (C++23).\n"
		"  Нужен libstdc++ >= 14, libc++ >= 18 (проще всего zig) или MSVC STL 17.10+.\n"
		"  Варианты: собрать драйвером zig (cmake/toolchains/zig-clang.cmake),\n"
		"  либо clang++ с -stdlib=libc++, либо cmake ... -DYO_BUILD_CLI=OFF.")
endif()

# Отладочная утилита: проверка правил РВ на контексте (не ставится).
add_executable(yo-regex-probe ../../tools/regex_probe.cpp)
target_link_libraries(yo-regex-probe PRIVATE yo::core)
