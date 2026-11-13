add_executable(yo_tests
	test_core.cpp
	test_yofy.cpp
	test_golden.cpp
	test_textbuf.cpp
	test_editor.cpp
	test_wrap.cpp
	test_utf.cpp
	test_yofytext.cpp
	test_askpanel.cpp
	test_ini.cpp
	test_find.cpp
	test_mru.cpp
	test_optionspanel.cpp
	test_usersel.cpp
	test_dictedit.cpp
	test_ranges.cpp
)

target_link_libraries(yo_tests PRIVATE yo::core Catch2::Catch2WithMain)

target_compile_definitions(yo_tests PRIVATE
	YO_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data"
	YO_CORPUS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/corpus"
	YO_GOLDEN_DIR="${CMAKE_CURRENT_SOURCE_DIR}/golden"
)

set_target_properties(yo_tests PROPERTIES CXX_STANDARD 23 CXX_STANDARD_REQUIRED ON)
# Ссылка с ядром должна собираться теми же флагами LTO, что и ядро.
yo_apply_release_tweaks(yo_tests)

# Макросы Catch2 (TEST_CASE) используют __COUNTER__ — в clang 21 это
# «расширение C2y», и с -Werror оно становится ошибкой сборки тестов.
# Флаг понимает только clang: GCC на незнакомый -Wno-error=... ругается
# («no option -Wc2y-extensions»), поэтому добавляем его лишь для Clang.
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
	target_compile_options(yo_tests PRIVATE -Wno-error=c2y-extensions)
endif()

include(Catch)

if(MSVC)
	# catch_discover_tests передаёт имя кейса аргументом, а на Windows узкие
	# строки командной строки разбираются в системной ANSI-кодепейдже (1252 на
	# раннерах): кириллица становится «?», Catch2 не находит кейсов и все они
	# «падают» за 0.01 с. Манифест activeCodePage=UTF-8 переключает CRT на UTF-8
	# (Windows 10 1903+); /MANIFESTINPUT понимают и link.exe, и lld-link.
	target_link_options(yo_tests PRIVATE
		"/MANIFEST:EMBED"
		"/MANIFESTINPUT:${CMAKE_CURRENT_SOURCE_DIR}/yo_tests.manifest")
endif()

catch_discover_tests(yo_tests)
