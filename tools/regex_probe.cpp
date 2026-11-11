// yo-regex-probe — отладочная утилита: показать трансляцию правил РВ
// и проверить их на заданном контексте.
//
// Использование:
//   yo-regex-probe <yo_regexpr.dat> [--dump] [--ctx "<контекст>" --word "<слово>"]

#include "yo/cp1251.hpp"
#include "yo/regexdict.hpp"

#include <cstdlib>
#include <cstring>
#include <print>
#include <string>
#include <vector>

using namespace yo;

int main(int argc, char** argv)
{
	if (argc < 2)
	{
		std::print(stderr, "использование: {} <yo_regexpr.dat> [--dump] [--ctx C --word W]\n",
				   argv[0]);
		return 1;
	}
	std::vector<LoadIssue> issues;
	auto rd = RegexDictionary::load(argv[1], &issues);
	if (!rd)
	{
		std::print(stderr, "ошибка: {}\n", rd.error());
		return 1;
	}
	std::println("правил: {}, версия: '{}', замечаний: {}", rd->size(), rd->version(),
				 issues.size());
	for (const auto& i : issues)
		std::println("  {}: {}", i.kind, cp1251::encode(cp1251::decode(i.detail)));

	UString ctx;
	UString word;
	UString w2_override;
	int di = -1;
	bool dump = false;
	for (int i = 2; i < argc; ++i)
	{
		const std::string_view a = argv[i];
		if (a == "--dump")
			dump = true;
		else if (a == "--ctx" && i + 1 < argc)
			ctx = cp1251::decode(argv[++i]);
		else if (a == "--word" && i + 1 < argc)
			word = cp1251::decode(argv[++i]);
		else if (a == "--w2" && i + 1 < argc)
			w2_override = cp1251::decode(argv[++i]);
	}
	{
		// Проверка предфильтра: сколько правил попало в RE2::Set.
		re2::RE2::Options o;
		o.set_log_errors(true);
		re2::RE2::Set set(o, re2::RE2::UNANCHORED);
		int ok = 0;
		for (const auto& r : rd->rules())
		{
			std::string err;
			const std::string pat = r.ignore_case ? ("(?i)" + r.pattern) : r.pattern;
			if (set.Add(pat, &err) >= 0)
				++ok;
			else
				std::println("Add failed: '{}' err='{}'", pat, err);
		}
		std::println("set: добавлено {}/{}", ok, rd->size());
		set.Compile();
		std::vector<int> ids;
		const std::string text = "vse ravno";
		std::println("test match '{}': {}", text, set.Match(text, &ids));
	}

	if (dump)
	{
		for (std::size_t i = 0; i < rd->size(); ++i)
		{
			const auto& r = rd->rules()[i];
			std::println("{:3d} '{}'\n    -> '{}'\n    -> '{}' ic={} lead={} trail={}", i + 1,
						 cp1251::encode(r.raw), cp1251::encode(r.valid), r.pattern, r.ignore_case,
						 r.lead_boundary, r.trail_boundary);
		}
	}
	if (const char* pat = std::getenv("YO_PROBE_PATTERN"); pat != nullptr)
	{
		re2::RE2::Options o;
		o.set_log_errors(true);
		o.set_dot_nl(true);
		re2::RE2 re(pat, o);
		std::println("pattern ok={} error='{}' groups={}", re.ok(), re.error(),
					 re.NumberOfCapturingGroups());
		const char* text = std::getenv("YO_PROBE_TEXT") != nullptr ? std::getenv("YO_PROBE_TEXT")
																   : "";
		re2::StringPiece m[4];
		const bool hit = re.Match(re2::StringPiece(text), 0, std::strlen(text),
								  re2::RE2::UNANCHORED, m, 4);
		std::println("text='{}' hit={} sub0='{}' sub1='{}'", text, hit,
					 hit ? std::string(m[0]) : std::string(), hit && m[1].data() ? std::string(m[1]) : std::string());
	}

	if (!ctx.empty() && !word.empty())
	{
		const auto pos = ctx.find(word);
		if (pos == UString::npos)
		{
			std::println("слово не найдено в контексте");
			return 2;
		}
		di = static_cast<int>(pos);
		const UString w1 = cyr::to_ye(word);
		const UString w2 = w2_override.empty() ? cyr::to_ye(word) : w2_override;
		int wn = 0;
		std::size_t rule_i = 0;
		const bool ok = rd->lookup(ctx, static_cast<std::size_t>(di), w1, w2, wn, rule_i);
		std::println("ctx='{}' di={} w1='{}' w2='{}' -> found={} word_n={} rule={}", 
					 cp1251::encode(ctx), di, cp1251::encode(w1), cp1251::encode(w2), ok, wn,
					 rule_i + 1);
	}
	return 0;
}
