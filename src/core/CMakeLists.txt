add_library(yo_core STATIC
	src/cp1251.cpp
	src/codec.cpp
	src/dict.cpp
	src/regexdict.cpp
	src/yofy.cpp
	src/mapped.cpp
	src/textbuf.cpp
	src/editor.cpp
	src/utf.cpp
	src/yofytext.cpp
	src/askpanel.cpp
	src/find.cpp
	src/ini.cpp
)
add_library(yo::core ALIAS yo_core)

target_include_directories(yo_core PUBLIC include)
target_link_libraries(yo_core PUBLIC yo_warnings yo_sanitizers)

if(YO_WITH_RE2)
	target_link_libraries(yo_core PUBLIC re2::re2)
	target_compile_definitions(yo_core PUBLIC YO_WITH_RE2=1)
else()
	target_compile_definitions(yo_core PUBLIC YO_WITH_RE2=0)
endif()

set_target_properties(yo_core PROPERTIES CXX_STANDARD 23 CXX_STANDARD_REQUIRED ON)
