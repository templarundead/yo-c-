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

std::string encode_utf8(const UString& s) { return encode_line(s, Encoding::utf8); }

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

TEST_CASE("ёфикация: вопросы движка и ответы пользователя", "[yofytext]")
{
	Engine e;
	// always_ask — как «всегда спрашивать» в оригинале: спрашиваем про каждое
	// слово, у которого есть вариант с ё.
	Options opts;
	opts.always_ask = true;
	e.yofy.set_options(opts);

	auto buf = tb("\u0432\u0441\u0435 \u0435\u0449\u0435\n");
	std::vector<std::string> asked_words;
	e.yofy.set_ask([&](const AskRequest& req) {
		asked_words.push_back(encode_utf8(req.word1));
		// Первое слово — «Е», второе — «Ё».
		return asked_words.size() == 1 ? AskAnswer::ye : AskAnswer::yo;
	});

	const YofyResult r = yofify_range(*buf, 0, buf->size(), e.yofy);
	CHECK(r.asked == 2);
	CHECK(asked_words.size() == 2);
	CHECK(r.changed);
	CHECK(buf->text() == "\u0432\u0441\u0435 \u0435\u0449\u0451\n");
}

TEST_CASE("ёфикация: «Откат» снимает последнюю замену, «Прервать» останавливает проход", "[yofytext]")
{
	Engine e;
	Options opts;
	opts.always_ask = true;
	e.yofy.set_options(opts);

	SECTION("откат на третьем вопросе оставляет первые ответы")
	{
		// Три вопроса: «все», «еще», «все». Первые два — «Ё», на третьем
		// «Откат»: он отменяет ПРЕДЫДУЩУЮ замену («ещё» → «еще»), а первые
		// ответы остаются в документе. Раньше откат сбрасывал весь проход.
		auto buf = tb("\u0432\u0441\u0435 \u0435\u0449\u0435 \u0432\u0441\u0435\n");
		std::size_t n = 0;
		e.yofy.set_ask([&](const AskRequest&) { return ++n == 3 ? AskAnswer::undo : AskAnswer::yo; });
		const YofyResult r = yofify_range(*buf, 0, buf->size(), e.yofy);
		CHECK_FALSE(r.cancelled);
		CHECK(r.changed);
		CHECK(r.reverted == 1);
		CHECK(buf->text() == "\u0432\u0441\u0451 \u0435\u0449\u0435 \u0432\u0441\u0435\n");
	}

	SECTION("откат, когда отменять нечего — как «пропустить»")
	{
		auto buf = tb("\u0432\u0441\u0435 \u0435\u0449\u0435\n");
		std::size_t n = 0;
		e.yofy.set_ask([&](const AskRequest&) { return ++n == 1 ? AskAnswer::undo : AskAnswer::yo; });
		const YofyResult r = yofify_range(*buf, 0, buf->size(), e.yofy);
		CHECK_FALSE(r.cancelled);
		CHECK(r.changed);
		CHECK(r.reverted == 0);
		CHECK(buf->text() == "\u0432\u0441\u0435 \u0435\u0449\u0451\n");
	}

	SECTION("откат последней замены возвращает документ к исходному")
	{
		// «все все»: первое слово — «Ё», на втором «Откат» снимает его.
		auto buf = tb("\u0432\u0441\u0435 \u0432\u0441\u0435\n");
		const std::string before = buf->text();
		std::size_t n = 0;
		e.yofy.set_ask([&](const AskRequest&) { return ++n == 1 ? AskAnswer::yo : AskAnswer::undo; });
		const YofyResult r = yofify_range(*buf, 0, buf->size(), e.yofy);
		CHECK_FALSE(r.changed);
		CHECK(buf->text() == before);
		CHECK_FALSE(buf->can_undo());
	}

	SECTION("прервать на второй строке")
	{
		auto buf = tb("\u0432\u0441\u0435 \u0435\u0449\u0435\n\u0432\u0441\u0435 \u0435\u0449\u0435\n");
		std::size_t n = 0;
		e.yofy.set_ask([&](const AskRequest&) {
			++n;
			if (n <= 2)
				return AskAnswer::yo;
			return AskAnswer::cancel;
		});
		const YofyResult r = yofify_range(*buf, 0, buf->size(), e.yofy);
		CHECK(r.cancelled);
		CHECK_FALSE(r.rolled_back);
		CHECK(r.changed);
		// Первая строка ёфицирована целиком, вторая прервана на первом слове.
		CHECK(buf->line_count() == 3);
		const std::string first = std::string(buf->line_text(0));
		CHECK(first == "\u0432\u0441\u0451 \u0435\u0449\u0451");
	}
}

TEST_CASE("ёфикация: «Пропустить» не меняет слово, проход продолжается", "[yofytext]")
{
	Engine e;
	Options opts;
	opts.always_ask = true;
	e.yofy.set_options(opts);

	auto buf = tb("\u0432\u0441\u0435 \u0435\u0449\u0435\n");
	std::size_t n = 0;
	e.yofy.set_ask([&](const AskRequest&) { return ++n == 1 ? AskAnswer::skip : AskAnswer::yo; });
	const YofyResult r = yofify_range(*buf, 0, buf->size(), e.yofy);
	CHECK_FALSE(r.cancelled);
	CHECK(r.changed);
	CHECK(buf->text() == "\u0432\u0441\u0435 \u0435\u0449\u0451\n");
}

TEST_CASE("ёфикация: on_line вызывается по строкам по порядку", "[yofytext]")
{
	Engine e;
	auto buf = tb("a\nb\nc\nd\n");
	std::vector<std::size_t> lines;
	yofify_range(*buf, 0, buf->size(), e.yofy, [&](std::size_t line) { lines.push_back(line); });
	CHECK(lines == std::vector<std::size_t>{ 0, 1, 2, 3 });

	lines.clear();
	yofify_range(*buf, buf->line_start(2), buf->size(), e.yofy,
				 [&](std::size_t line) { lines.push_back(line); });
	CHECK(lines == std::vector<std::size_t>{ 2, 3 });
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

TEST_CASE("ёфикация: внешняя отмена прекращает проход", "[yofytext]")
{
	Engine e;
	// Текст без ё-неоднозначностей: проверяем именно механику отмены.
	std::string text;
	for (int i = 0; i < 500; ++i)
		text += "строка без замен номер " + std::to_string(i) + "\n";
	auto buf = tb(text);

	std::size_t seen = 0;
	const YofyResult r = yofify_range(
		*buf, 0, buf->size(), e.yofy, [&](std::size_t) { ++seen; }, [&] { return seen >= 10; });

	CHECK(r.cancelled);
	CHECK_FALSE(r.rolled_back);
	// Отмена приходит перед обработкой строки: девять строк обработано,
	// на десятой проход остановился — а не прошёл все 500.
	CHECK(r.lines == 9);
	CHECK(seen == 10);
}

TEST_CASE("ёфикация: без отмены проход идёт до конца", "[yofytext]")
{
	Engine e;
	std::string text;
	for (int i = 0; i < 50; ++i)
		text += "еще одна строка " + std::to_string(i) + "\n";
	auto buf = tb(text);
	std::size_t seen = 0;
	const YofyResult r = yofify_range(
		*buf, 0, buf->size(), e.yofy, [&](std::size_t) { ++seen; }, [&] { return false; });
	CHECK_FALSE(r.cancelled);
	CHECK(r.lines == 50);
	CHECK(seen == 50);
}
