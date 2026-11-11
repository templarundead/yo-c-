// Golden-тесты: сравнение ядра с независимой эталонной реализацией
// (tools/golden.py, порт Main.pas на Python).
//
// Для каждой политики неоднозначности (skip/ye/yo) и каждого файла корпуса
// проверяются: текст после ёфикации и счётчики статистики.

#include "yo/codec.hpp"
#include "yo/cp1251.hpp"
#include "yo/dict.hpp"
#include "yo/regexdict.hpp"
#include "yo/yofy.hpp"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using namespace yo;

namespace {

const auto kCorpusDir = std::filesystem::path(YO_CORPUS_DIR);
const auto kGoldenDir = std::filesystem::path(YO_GOLDEN_DIR);
const auto kDataDir = std::filesystem::path(YO_DATA_DIR);

std::string read_file(const std::filesystem::path& p)
{
	std::ifstream in(p, std::ios::binary);
	REQUIRE(in);
	std::string s;
	in.seekg(0, std::ios::end);
	s.resize(static_cast<std::size_t>(in.tellg()));
	in.seekg(0);
	in.read(s.data(), static_cast<std::streamsize>(s.size()));
	return s;
}

struct Doc : LineSource
{
	std::vector<UString> lines;
	std::vector<std::string> eols;
	std::size_t line_count() const override { return lines.size(); }
	const UString* line(std::size_t i) const override
	{
		return i < lines.size() ? &lines[i] : nullptr;
	}
};

struct Env
{
	Dictionary dict;
	RegexDictionary rd;

	Env()
	{
		std::vector<LoadIssue> issues;
		auto d = Dictionary::load(kDataDir / "yox.dat", &issues);
		REQUIRE(d.has_value());
		dict = std::move(*d);
		auto r = RegexDictionary::load(kDataDir / "yo_regexpr.dat", &issues);
		REQUIRE(r.has_value());
		rd = std::move(*r);
	}
};

Env& env()
{
	static Env e;
	return e;
}

// Разбор текста (UTF-8) на строки с сохранением байтов EOL.
Doc split_utf8(std::string_view text)
{
	Doc doc;
	std::size_t start = 0;
	while (start <= text.size())
	{
		const auto pos = text.find('\n', start);
		const auto end = pos == std::string_view::npos ? text.size() : pos;
		std::string_view piece = text.substr(start, end - start);
		std::string eol;
		if (!piece.empty() && piece.back() == '\r')
		{
			piece.remove_suffix(1);
			eol = "\r\n";
		}
		else if (pos != std::string_view::npos)
			eol = "\n";
		doc.lines.push_back(decode_line(
			{ reinterpret_cast<const std::uint8_t*>(piece.data()), piece.size() }, Encoding::utf8));
		doc.eols.push_back(eol);
		if (pos == std::string_view::npos)
			break;
		start = pos + 1;
	}
	return doc;
}

std::string join_utf8(const Doc& doc)
{
	std::string out;
	for (std::size_t i = 0; i < doc.lines.size(); ++i)
	{
		out += encode_line(doc.lines[i], Encoding::utf8);
		out += doc.eols[i];
	}
	return out;
}

AskAnswer answer_for(std::string_view policy)
{
	if (policy == "ye")
		return AskAnswer::ye;
	if (policy == "yo")
		return AskAnswer::yo;
	return AskAnswer::skip;
}

std::string process(std::string_view text, std::string_view policy, Stats& stats)
{
	Doc doc = split_utf8(text);
	Options opts;
	Yoficator yofy(env().dict, &env().rd, opts);
	const AskAnswer ans = answer_for(policy);
	yofy.set_ask([ans](const AskRequest&) { return ans; });
	for (std::size_t i = 0; i < doc.lines.size(); ++i)
	{
		if (!yofy.process_line(doc.lines[i], i, doc, nullptr))
			break;
	}
	stats = yofy.stats();
	return join_utf8(doc);
}

// Ожидаемые счётчики из stats.json (минимальный разбор без JSON-библиотеки).
struct ExpectedCounts
{
	int checked_a = -1;
	int changed = -1;
	int asked_a = -1;
	int re_a = -1;
	int ambiguous = -1;
};

ExpectedCounts read_stats(const std::filesystem::path& path, std::string_view file_name)
{
	ExpectedCounts e;
	const std::string text = read_file(path);
	const std::string key = "\"" + std::string(file_name) + "\"";
	const auto obj_start = text.find(key);
	if (obj_start == std::string::npos)
		return e;
	const auto obj_end = text.find('}', obj_start);
	const std::string body = text.substr(obj_start, obj_end - obj_start);
	auto number = [&body](std::string_view field) -> int
	{
		const std::string needle = "\"" + std::string(field) + "\":";
		const auto p = body.find(needle);
		if (p == std::string::npos)
			return -1;
		return std::stoi(body.substr(p + needle.size()));
	};
	e.checked_a = number("checked_a");
	e.changed = number("checked_changed");
	e.asked_a = number("asked_a");
	e.re_a = number("re_a");
	e.ambiguous = number("ambiguous");
	return e;
}

} // namespace

TEST_CASE("golden: корпус совпадает с эталонной реализацией", "[golden]")
{
	REQUIRE(std::filesystem::exists(kCorpusDir));

	std::vector<std::filesystem::path> corpus;
	for (const auto& entry : std::filesystem::directory_iterator(kCorpusDir))
		if (entry.path().extension() == ".txt")
			corpus.push_back(entry.path());
	REQUIRE(corpus.size() >= 6);

	for (const std::string policy : { "skip", "ye", "yo" })
	{
		const auto stats_path = kGoldenDir / policy / "stats.json";
		REQUIRE(std::filesystem::exists(stats_path));

		for (const auto& file : corpus)
		{
			const auto expected_path =
				kGoldenDir / policy / (file.stem().string() + ".expected");
			INFO("файл: " << file.filename().string() << ", политика: " << policy);
			REQUIRE(std::filesystem::exists(expected_path));

			const std::string input = read_file(file);
			const std::string expected = read_file(expected_path);
			Stats stats;
			const std::string actual = process(input, policy, stats);
			CHECK(actual == expected);

			const ExpectedCounts counts = read_stats(stats_path, file.filename().string());
			CHECK(static_cast<int>(stats.checked.a) == counts.checked_a);
			CHECK(static_cast<int>(stats.checked.changed) == counts.changed);
			CHECK(static_cast<int>(stats.asked.a) == counts.asked_a);
			CHECK(static_cast<int>(stats.regex_resolved.a) == counts.re_a);
		}
	}
}
