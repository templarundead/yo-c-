// Тесты движка ёфикации: логика SelectWord, гейты опций, статистика.

#include "yo/codec.hpp"
#include "yo/cp1251.hpp"
#include "yo/dict.hpp"
#include "yo/regexdict.hpp"
#include "yo/yofy.hpp"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <string>
#include <vector>

using namespace yo;

namespace {

const auto kDataDir = std::filesystem::path(YO_DATA_DIR);

UString u(std::string_view utf8)
{
	return decode_line({ reinterpret_cast<const std::uint8_t*>(utf8.data()), utf8.size() },
					   Encoding::utf8);
}
std::string enc(UStringView s) { return encode_line(s, Encoding::utf8); }

struct Doc : LineSource
{
	std::vector<UString> lines;
	std::size_t line_count() const override { return lines.size(); }
	const UString* line(std::size_t i) const override
	{
		return i < lines.size() ? &lines[i] : nullptr;
	}
};

struct H : Doc
{
	Dictionary dict;
	RegexDictionary rd;
	std::vector<AskRequest> asks;
	AskAnswer answer = AskAnswer::ye;
	Options opts;

	H()
	{
		std::vector<LoadIssue> issues;
		auto d = Dictionary::load(kDataDir / "yox.dat", &issues);
		REQUIRE(d.has_value());
		dict = std::move(*d);
		auto r = RegexDictionary::load(kDataDir / "yo_regexpr.dat", &issues);
		REQUIRE(r.has_value());
		rd = std::move(*r);
	}

	std::string run(std::string_view text)
	{
		lines.clear();
		std::string s(text);
		std::size_t start = 0;
		while (start <= s.size())
		{
			const auto pos = s.find('\n', start);
			const auto end = pos == std::string::npos ? s.size() : pos;
			std::string piece = s.substr(start, end - start);
			if (!piece.empty() && piece.back() == '\r')
				piece.pop_back();
			lines.push_back(u(piece));
			if (pos == std::string::npos)
				break;
			start = pos + 1;
		}
		Yoficator yofy(dict, rd.empty() ? nullptr : &rd, opts);
		yofy.set_ask([this](const AskRequest& r)
					 {
						 asks.push_back(r);
						 return answer;
					 });
		for (std::size_t i = 0; i < lines.size(); ++i)
		{
			if (!yofy.process_line(lines[i], i, *this, nullptr))
				break;
		}
		last_stats = yofy.stats();
		std::string out;
		for (std::size_t i = 0; i < lines.size(); ++i)
		{
			if (i > 0)
				out += '\n';
			out += enc(lines[i]);
		}
		return out;
	}

	Stats last_stats;
};

} // namespace

TEST_CASE("автоматические замены без вопросов", "[yofy]")
{
	H h;
	const std::string out = h.run("он шел и принес еще один словарь");
	CHECK(out == "он шёл и принёс ещё один словарь");
	CHECK(h.asks.empty());
	CHECK(h.last_stats.checked.a == 3);
	CHECK(h.last_stats.checked.changed == 3);
	CHECK(h.last_stats.wrong_yo == 0);
}

TEST_CASE("слово не меняется, если ёфикация выключена для ё-слов", "[yofy]")
{
	H h;
	h.opts.check_yo = false;
	const std::string out = h.run("он шёл и принес еще");
	// «шёл» не проверяется (CheckYo=off), «принес»/«еще» — проверяются.
	CHECK(out == "он шёл и принёс ещё");
}

TEST_CASE("короткие слова и слова без е/ё не проверяются", "[yofy]")
{
	H h;
	(void)h.run("я и он, дом, кот");
	CHECK(h.last_stats.checked.a == 0);
}

TEST_CASE("неоднозначное слово: skip оставляет как есть", "[yofy]")
{
	H h;
	h.answer = AskAnswer::skip;
	const std::string out = h.run("он осел у дороги");
	CHECK(out == "он осел у дороги");
	CHECK(h.asks.size() == 1);
	CHECK(h.last_stats.ambiguous_skipped == 1);
}

TEST_CASE("неоднозначное слово: ответ ё меняет слово и запоминается", "[yofy]")
{
	H h;
	h.answer = AskAnswer::yo;
	const std::string out = h.run("он осел у дороги");
	CHECK(out == "он осёл у дороги");
	CHECK(h.asks.size() == 1);
	CHECK(h.last_stats.asked.a == 1);
	CHECK(h.last_stats.asked.changed == 1);
}

TEST_CASE("«всегда ё» запоминается в словаре на весь сеанс", "[yofy]")
{
	H h;
	h.answer = AskAnswer::yo_all;
	const std::string out = h.run("он осел у дороги\nосел стоял рядом");
	// Первый вопрос — «всегда ё», второй уже без вопроса.
	CHECK(h.asks.size() == 1);
	CHECK(out == "он осёл у дороги\nосёл стоял рядом");
}

TEST_CASE("праивло РВ снимает неоднозначность", "[yofy]")
{
	H h;
	h.answer = AskAnswer::skip;
	const std::string out = h.run("он все равно придет");
	CHECK(out == "он всё равно придёт");
	CHECK(h.asks.empty());
	CHECK(h.last_stats.regex_resolved.a == 1);
}

TEST_CASE("NoVarOnly пропускает неоднозначные слова", "[yofy]")
{
	H h;
	h.opts.no_var_only = true;
	h.answer = AskAnswer::yo;
	const std::string out = h.run("он осел у дороги и шел домой");
	CHECK(out == "он осел у дороги и шёл домой");
	CHECK(h.asks.empty());
}

TEST_CASE("VarOnly оставляет только интерактивные замены", "[yofy]")
{
	H h;
	h.opts.var_only = true;
	h.answer = AskAnswer::yo;
	const std::string out = h.run("он осел у дороги и шел домой");
	CHECK(out == "он осёл у дороги и шел домой");
}

TEST_CASE("AlwaysAsk спрашивает даже про однозначные слова", "[yofy]")
{
	H h;
	h.opts.always_ask = true;
	h.answer = AskAnswer::ye;
	const std::string out = h.run("он шел домой");
	CHECK(out == "он шел домой");
	CHECK(h.asks.size() == 1);
	CHECK(h.last_stats.asked.a == 1);
}

TEST_CASE("FBFormat обрезает строку по тегу <binary", "[yofy]")
{
	H h;
	h.opts.fb_format = true;
	const std::string out = h.run("он шел домой <binary id=\"x\">шел");
	CHECK(out == "он шёл домой <binary id=\"x\">шел");
}

TEST_CASE("многоточие требует подтверждения", "[yofy]")
{
	H h;
	h.answer = AskAnswer::skip;
	CHECK(h.run("он шел...") == "он шел...");
	CHECK(h.asks.size() == 1);

	H h2;
	h2.opts.to_confirm_ellipsis = false;
	h2.answer = AskAnswer::skip;
	CHECK(h2.run("он шел...") == "он шёл...");
	CHECK(h2.asks.empty());
}

TEST_CASE("сокращение требует подтверждения", "[yofy]")
{
	H h;
	h.answer = AskAnswer::skip;
	// «пришел.» + строчная буква дальше — похоже на сокращение: спрашиваем.
	CHECK(h.run("он пришел. еще рано") == "он пришел. ещё рано");
	CHECK(h.asks.size() == 1);

	H h2;
	h2.opts.to_confirm_abbr = false;
	h2.answer = AskAnswer::skip;
	CHECK(h2.run("он пришел. еще рано") == "он пришёл. ещё рано");
	CHECK(h2.asks.empty());
}

TEST_CASE("статистика: счётчики совпадают с ожидаемыми", "[yofy]")
{
	H h;
	h.answer = AskAnswer::skip;
	(void)h.run("все это время он шел и пел");
	const auto& s = h.last_stats;
	CHECK(s.checked.a == s.checked.changed + s.checked.not_changed);
	CHECK(s.asked.a == s.asked.changed + s.asked.not_changed);
	CHECK(s.regex_resolved.a <= s.checked.a);
}
