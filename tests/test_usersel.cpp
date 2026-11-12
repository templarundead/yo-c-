// Запомненные ответы «Всегда Е/Ё»: разбор и запись файла.

#include "yo/usersel.hpp"

#include <catch2/catch_test_macros.hpp>

using namespace yo;

TEST_CASE("user sel: разбор файла", "[usersel]")
{
	const std::string text =
		"; Ё (yo-c-) — запомненные ответы «Всегда Е/Ё»\n"
		"# ещё комментарий\n"
		"\n"
		"все\t2\n"
		"ещё\t1\n"
		"битый\n"		  // нет табуляции — пропускаем
		"мусор\t7\n"	  // 7 — не ответ
		"\t2\n"			  // пустое слово
		" ещё \t 1 \r\n"; // пробелы по краям не мешают
	const std::vector<UserSelEntry> parsed = parse_user_selections(text);
	REQUIRE(parsed.size() == 3);
	CHECK(parsed[0].word == "все");
	CHECK(parsed[0].value == 2);
	CHECK(parsed[1].word == "ещё");
	CHECK(parsed[1].value == 1);
	CHECK(parsed[2].word == "ещё");
	CHECK(parsed[2].value == 1);
}

TEST_CASE("user sel: запись и повторное чтение", "[usersel]")
{
	const std::vector<UserSelEntry> entries = {
		{ "все", 2 },
		{ "ещё", 1 },
		{ "плохой", 0 }, // такие не пишем
	};
	const std::string text = format_user_selections(entries);
	CHECK(text == "все\t2\nещё\t1\n");

	const std::vector<UserSelEntry> back = parse_user_selections(text);
	REQUIRE(back.size() == 2);
	CHECK(back[0].word == "все");
	CHECK(back[0].value == 2);
	CHECK(back[1].word == "ещё");
	CHECK(back[1].value == 1);
}

TEST_CASE("user sel: пустой файл и мусор", "[usersel]")
{
	CHECK(parse_user_selections("").empty());
	CHECK(parse_user_selections("\n\n\n").empty());
	CHECK(parse_user_selections("; только комментарий\n").empty());
	CHECK(format_user_selections({}).empty());
}
