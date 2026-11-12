// Словарь регулярных выражений yo_regexpr.dat.
//
// Шаблоны приводятся к виду оригинала (MakeValidRegExpr), затем
// транслируются в синтаксис RE2: \w, \W, \s, \S, \d, \D расширяются
// до классов оригинала (WordChars/SpaceChars), краевые \b снимаются и
// проверяются вручную (см. FORMATS.md §2.3).
#pragma once

#include "yo/cyr.hpp"
#include "yo/dict.hpp"

#include <cstdint>
#include <expected>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include <re2/re2.h>
#include <re2/set.h>

namespace yo {

// MakeValidRegExpr: blanks -> \s+, е/ё -> ([её]) вне классов, '-' -> -(\r\n){0,1},
// краевые \b для слов, начинающихся/заканчивающихся буквой.
[[nodiscard]] UString make_valid_regexp(UStringView rule);

struct RuleBuildInfo
{
	bool lead_boundary = false;
	bool trail_boundary = false;
	std::vector<std::string> warnings;	// например, \b внутри шаблона
};

// Трансляция валидного шаблона в синтаксис RE2 (UTF-8).
[[nodiscard]] std::string translate_to_re2(UStringView valid, RuleBuildInfo& info);

struct RegexRule
{
	UString raw;					   // исходное правило
	UString valid;					   // после MakeValidRegExpr
	std::string pattern;			   // UTF-8 для RE2
	bool ignore_case = true;
	bool lead_boundary = false;
	bool trail_boundary = false;
	std::vector<std::size_t> ye_yo;	   // 0-based позиции е/ё в raw (для подстановок)
	std::uint64_t was = 0;			   // сколько раз сработало (как Dic.Was)
	std::unique_ptr<re2::RE2> re;
};

class RegexDictionary
{
public:
	static std::expected<RegexDictionary, std::string> load(const std::filesystem::path& path,
															std::vector<LoadIssue>* issues = nullptr);
	// То же по тексту словаря (CP1251) — для редактора словарей: он проверяет
	// правленые правила без записи во временный файл.
	static std::expected<RegexDictionary, std::string> parse(std::string_view cp1251_text,
															 std::vector<LoadIssue>* issues = nullptr);

	[[nodiscard]] bool empty() const noexcept { return rules_.empty(); }
	[[nodiscard]] std::size_t size() const noexcept { return rules_.size(); }
	[[nodiscard]] const std::string& version() const noexcept { return version_; }
	[[nodiscard]] const std::vector<RegexRule>& rules() const noexcept { return rules_; }

	// MatchRegExp: проверка одного правила на контексте.
	// Возвращает WordN (1 — совпало с word1, 2 — с word2, 0 — нет).
	[[nodiscard]] int match_rule(const RegexRule& rule, UStringView context, std::size_t di,
								 UStringView word1, UStringView word2) const;

	// LookInRegExps: первое сработавшее правило (по порядку словаря).
	// out_rule — индекс правила, out_word_n — результат.
	bool lookup(UStringView context, std::size_t di, UStringView word1, UStringView word2,
				int& out_word_n, std::size_t& out_rule) const;

	void note_used(std::size_t rule_index) noexcept { ++rules_[rule_index].was; }

private:
	// Контекст в UTF-8 с картой «байт -> кодпоинт» (строится один раз на слово).
	struct ContextUtf8
	{
		std::string bytes;
		std::vector<std::uint32_t> byte_to_cp;
	};

	void build_set();
	static void fill_utf8_map(ContextUtf8& out, UStringView context);
	[[nodiscard]] int match_rule_utf8(const RegexRule& rule, UStringView context,
									  const ContextUtf8& ctx8, std::size_t di, UStringView word1,
									  UStringView word2) const;

	std::vector<RegexRule> rules_;
	std::unique_ptr<re2::RE2::Set> set_;	// предфильтр по всем правилам
	std::vector<std::size_t> set_ids_;		// id в Set -> индекс правила
	std::string version_;
};

} // namespace yo
