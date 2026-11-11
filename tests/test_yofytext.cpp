// Ёфикация документа через TextBuffer: строки, диапазон, EOL, undo.

#include "yo/codec.hpp"
#include "yo/dict.hpp"
#include "yo/mapped.hpp"
#include "yo/regexdict.hpp"
#include "yo/textbuf.hpp"
#include "yo/yofy.hpp"
#include "yo/yofytext.hpp"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <memory>
#include <string>

using namespace yo;

namespace {

const auto kDataDir = std::filesystem::path(YO_DATA_DIR);

struct Engine
{
	Dictionary dict;
	RegexDictionary rd;
	Yoficator yofy;

	Engine()
		: dict(load_dict()), rd(load_regex()), yofy(dict, &rd, Options{})
	{
		// Без AskPanel все вопросы решаются «Ё» (как «Всегда Ё»).
		yofy.set_ask([](const AskRequest&) { return AskAnswer::yo; });
	}

	static Dictionary load_dict()
	{
		std::vector<LoadIssue> issues;
		auto d = Dictionary::load(kDataDir / "yox.dat", &issues);
		REQUIRE(d.has_value());
		return std::move(*d);
	}

	static RegexDictionary load_regex()
	{
		std::vector<LoadIssue> issues;
		auto r = RegexDictionary::load(kDataDir / "yo_regexpr.dat", &issues);
		REQUIRE(r.has_value());
		return std::move(*r);
	}
};

std::shared_ptr<TextBuffer> tb(std::string text)
{
	return std::make_shared<TextBuffer>(std::make_shared<MemorySource>(std::move(text)));
}

} // namespace

TEST_CASE("ёфикация документа: строки меняются, EOL сохраняются", "[yofytext]")
{
	Engine e;
	auto buf = tb("Все еще не пришел.\r\nОн все равно придет.\n");
	const std::string before = buf->text();

	// Одна группа undo на весь проход.
	const YofyResult r = yofify_range(*buf, 0, buf->size(), e.yofy);
	CHECK(r.changed);
	CHECK(r.lines == 2);
	CHECK(r.replaced > 0);

	const std::string after = buf->text();
	INFO(after);
	CHECK(after.find("ещё") != std::string::npos);
	CHECK(after.find("пришёл") != std::string::npos);
	CHECK(after.find("придёт") != std::string::npos);
	// Переводы строк не тронуты, разбиение на строки прежнее.
	CHECK(buf->line_count() == 3);
	CHECK(buf->line_eol(0) == Eol::crlf);
	CHECK(buf->line_eol(1) == Eol::lf);
	CHECK(after.size() == before.size()); // е<->ё меняют только байты, не длину

	// Одна отмена — исходный текст.
	REQUIRE(buf->undo());
	CHECK(buf->text() == before);
	CHECK(buf->line_count() == 3);
}

TEST_CASE("ёфикация диапазона: трогается только выделенное", "[yofytext]")
{
	Engine e;
	auto buf = tb("все еще\nвсе еще\n");
	const std::size_t second = buf->line_start(1);
	const YofyResult r = yofify_range(*buf, second, buf->size(), e.yofy);
	CHECK(r.changed);

	const std::string text = buf->text();
	// Первая строка не тронута, вторая ёфицирована.
	CHECK(text.rfind("все еще\n", 0) == 0);
	CHECK(text.find("всё ещё\n", 8) != std::string::npos);
	CHECK(buf->line_count() == 3);
}

TEST_CASE("ёфикация: нечего менять — буфер не меняется", "[yofytext]")
{
	Engine e;
	auto buf = tb("дом кот окно\n");
	const std::string before = buf->text();
	const YofyResult r = yofify_range(*buf, 0, buf->size(), e.yofy);
	CHECK_FALSE(r.changed);
	CHECK(buf->text() == before);
	CHECK_FALSE(buf->modified());
	CHECK_FALSE(buf->can_undo());
}

TEST_CASE("ёфикация: пустой документ и пустой диапазон", "[yofytext]")
{
	Engine e;
	auto empty = tb("");
	CHECK_FALSE(yofify_range(*empty, 0, 0, e.yofy).changed);
	CHECK(empty->size() == 0);

	auto buf = tb("все еще\n");
	CHECK_FALSE(yofify_range(*buf, 3, 3, e.yofy).changed);
}
