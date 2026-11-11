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
# Флаг понимает только clang: GCC на незнакомый -Wno-error=... ругается
# («no option -Wc2y-extensions»), поэтому добавляем его лишь для Clang.
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
	target_compile_options(yo_tests PRIVATE -Wno-error=c2y-extensions)
endif()

include(Catch)
if(MSVC)
	# catch_discover_tests запускает каждый кейс отдельным процессом, передавая
	# имя теста аргументом. На Windows кириллические имена доходят искажёнными,
	# Catch2 отвечает «No test cases matched» и все 67 кейсов «падают» за 0.01 с.
	# Поэтому на MSVC-ABI (MSVC и clang-cl) регистрируем весь бинарник одним
	# тестом — ctest всё равно покажет вывод при --output-on-failure.
	add_test(NAME yo_tests COMMAND yo_tests)
else()
	catch_discover_tests(yo_tests)
endif()
