// Поиск и замена по документу: регистр, слово целиком, все совпадения.

#include "yo/find.hpp"
#include "yo/textbuf.hpp"

#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <string>

using namespace yo;

namespace {

std::shared_ptr<TextBuffer> tb(std::string text)
{
	return std::make_shared<TextBuffer>(std::make_shared<MemorySource>(std::move(text)));
}

} // namespace

TEST_CASE("поиск: следующее и предыдущее вхождение", "[find]")
{
	auto buf = tb("кот собака кот\nпёс\n");
	const std::size_t first = find_next(*buf, 0, "кот");
	REQUIRE(first != std::string::npos);
	CHECK(buf->text().substr(first, 6) == "кот"); // в UTF-8 кириллица — 2 байта
	const std::size_t second = find_next(*buf, first + 1, "кот");
	REQUIRE(second != std::string::npos);
	CHECK(second > first);
	CHECK(find_next(*buf, second + 1, "кот") == std::string::npos);

	const std::size_t prev = find_prev(*buf, second, "кот");
	CHECK(prev == first);
	CHECK(find_prev(*buf, first, "кот") == std::string::npos);
}

TEST_CASE("поиск: регистр и кириллица", "[find]")
{
	auto buf = tb("Все всё ВСЁ\n");
	FindOptions ci;
	CHECK(find_all(*buf, 0, buf->size(), "всё", ci).size() == 2);

	FindOptions cs;
	cs.case_sensitive = true;
	const auto hits = find_all(*buf, 0, buf->size(), "всё", cs);
	REQUIRE(hits.size() == 1);
	CHECK(buf->text().substr(hits[0], 6) == "всё");
}

TEST_CASE("поиск: слово целиком", "[find]")
{
	auto buf = tb("кот котёнок кот\n");
	FindOptions opts;
	opts.whole_word = true;
	const auto hits = find_all(*buf, 0, buf->size(), "кот", opts);
	CHECK(hits.size() == 2); // «котёнок» — другое слово
	opts.case_sensitive = true;
	CHECK(find_all(*buf, 0, buf->size(), "кот", opts).size() == 2);
}

TEST_CASE("поиск: совпадения внутри строки", "[find]")
{
	CHECK(find_in_line("аа аа аа", "аа").size() == 3);
	CHECK(find_in_line("аааа", "аа").size() == 3); // перекрывающиеся тоже находятся
	FindOptions word;
	word.whole_word = true;
	CHECK(find_in_line("аааа аа", "аа", word).size() == 1);
}

TEST_CASE("замена: заменить всё одной группой undo", "[find]")
{
	auto buf = tb("кот и кот, ещё кот\n");
	const std::size_t n = replace_all(*buf, 0, buf->size(), "кот", "пёс");
	CHECK(n == 3);
	CHECK(buf->text() == "пёс и пёс, ещё пёс\n");
	REQUIRE(buf->undo());
	CHECK(buf->text() == "кот и кот, ещё кот\n");
}

TEST_CASE("замена: пустая замена удаляет найденное", "[find]")
{
	auto buf = tb("а-б-в\n");
	CHECK(replace_all(*buf, 0, buf->size(), "-", "") == 2);
	CHECK(buf->text() == "абв\n");
}

TEST_CASE("поиск РВ: образец и ошибка разбора", "[find]")
{
	auto buf = tb("Ёжик в тумане\nёлка и Ель\n");
	const FindRegexResult r = find_regex(*buf, 0, "Ё+");
	REQUIRE(r.found());
	CHECK(r.begin == 0);
	CHECK(r.end == 2); // «Ё» в UTF-8 — 2 байта

	// Регистронезависимость по умолчанию.
	const FindRegexResult lower = find_regex(*buf, 0, "ель");
	REQUIRE(lower.found());
	CHECK(buf->text().substr(lower.begin, lower.end - lower.begin) == "Ель");

	// С учётом регистра — только строчная «ёлка».
	const FindRegexResult cs = find_regex(*buf, 0, "ёл", true);
	REQUIRE(cs.found());
	CHECK(cs.begin == buf->text().find("ёлка"));

	const FindRegexResult bad = find_regex(*buf, 0, "(незакрытая");
	CHECK_FALSE(bad.found());
	CHECK_FALSE(bad.error.empty());
}

TEST_CASE("поиск РВ: назад и продолжение вперёд", "[find]")
{
	auto buf = tb("кот 12 кот 345 кот\n");
	const FindRegexResult first = find_regex(*buf, 0, "\\d+");
	REQUIRE(first.found());
	CHECK(buf->text().substr(first.begin, first.end - first.begin) == "12");

	const FindRegexResult second = find_regex(*buf, first.end, "\\d+");
	REQUIRE(second.found());
	CHECK(buf->text().substr(second.begin, second.end - second.begin) == "345");

	const FindRegexResult back = find_regex_prev(*buf, second.begin, "\\d+");
	REQUIRE(back.found());
	CHECK(buf->text().substr(back.begin, back.end - back.begin) == "12");

	CHECK_FALSE(find_regex_prev(*buf, first.begin, "\\d+").found());
}

TEST_CASE("поиск РВ: строка целиком и якоря", "[find]")
{
	auto buf = tb("первая\nвторая строка\nтретья\n");
	const FindRegexResult r = find_regex(*buf, 0, "^вторая.*$");
	REQUIRE(r.found());
	CHECK(buf->text().substr(r.begin, r.end - r.begin) == "вторая строка");
}
