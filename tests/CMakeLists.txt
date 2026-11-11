add_executable(yo_tests
	test_core.cpp
	test_yofy.cpp
	test_golden.cpp
	test_textbuf.cpp
	test_editor.cpp
	test_utf.cpp
	test_yofytext.cpp
	test_askpanel.cpp
)

target_link_libraries(yo_tests PRIVATE yo::core Catch2::Catch2WithMain)

target_compile_definitions(yo_tests PRIVATE
	YO_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data"
	YO_CORPUS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/corpus"
	YO_GOLDEN_DIR="${CMAKE_CURRENT_SOURCE_DIR}/golden"
)

set_target_properties(yo_tests PROPERTIES CXX_STANDARD 23 CXX_STANDARD_REQUIRED ON)

# Макросы Catch2 (TEST_CASE) используют __COUNTER__ — в clang 21 это
# «расширение C2y», и с -Werror оно становится ошибкой сборки тестов.
if(NOT MSVC)
	target_compile_options(yo_tests PRIVATE -Wno-error=c2y-extensions)
endif()

include(Catch)
catch_discover_tests(yo_tests)
