#include "yo/regexdict.hpp"

#include "yo/cp1251.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <fstream>
#include <string>
#include <vector>

#include "yo/cp1251.hpp"


#include <re2/re2.h>

namespace yo {

namespace {

void append_utf8(std::string& out, char32_t c)
{
	if (c < 0x80)
		out.push_back(static_cast<char>(c));
	else if (c < 0x800)
	{
		out.push_back(static_cast<char>(0xC0 | (c >> 6)));
		out.push_back(static_cast<char>(0x80 | (c & 0x3F)));
	}
	else if (c < 0x10000)
	{
		out.push_back(static_cast<char>(0xE0 | (c >> 12)));
		out.push_back(static_cast<char>(0x80 | ((c >> 6) & 0x3F)));
		out.push_back(static_cast<char>(0x80 | (c & 0x3F)));
	}
	else
	{
		out.push_back(static_cast<char>(0xF0 | (c >> 18)));
		out.push_back(static_cast<char>(0x80 | ((c >> 12) & 0x3F)));
		out.push_back(static_cast<char>(0x80 | ((c >> 6) & 0x3F)));
		out.push_back(static_cast<char>(0x80 | (c & 0x3F)));
	}
}

std::string to_utf8(UStringView s)
{
	std::string out;
	out.reserve(s.size() * 2);
	for (char32_t c : s)
		append_utf8(out, c);
	return out;
}

bool starts_with(UStringView s, UStringView prefix)
{
	return s.size() >= prefix.size() && s.substr(0, prefix.size()) == prefix;
}

bool ends_with(UStringView s, UStringView suffix)
{
	return s.size() >= suffix.size() && s.substr(s.size() - suffix.size()) == suffix;
}

} // namespace

UString make_valid_regexp(UStringView s)
{
	UString out;
	out.reserve(s.size() + 16);
	bool was_blank = false;
	bool was_backslash = false;
	int brackets = 0;
	for (char32_t ch : s)
	{
		if (cyr::is_blank(ch))
		{
			if (!was_blank)
				out += U"\\s+";
			else
				was_blank = true;
			continue;
		}
		was_blank = false;
		if (!was_backslash && (ch == U'(' || ch == U'['))
			++brackets;
		else if (!was_backslash && (ch == U')' || ch == U']'))
			--brackets;
		if (brackets == 0)
		{
			if (cyr::is_ye_yo(ch))
				out += cyr::is_upper(ch) ? U"([ЕЁ])" : U"([её])";
			else if (ch == U'-')
				out += U"-(\\r\\n){0,1}";
			else
				out.push_back(ch);
		}
		else
			out.push_back(ch);
		was_backslash = (ch == U'\\');
	}
	if (!out.empty() && cyr::is_alpha(out.front()))
		out.insert(0, U"\\b");
	if (!out.empty() && cyr::is_alpha(out.back()))
		out += U"\\b";
	return out;
}

std::string translate_to_re2(UStringView valid, RuleBuildInfo& info)
{
	UString body(valid);
	if (starts_with(body, U"\\b"))
	{
		info.lead_boundary = true;
		body.erase(0, 2);
	}
	if (ends_with(body, U"\\b"))
	{
		info.trail_boundary = true;
		body.erase(body.size() - 2);
	}

	UString out;
	out.reserve(body.size() + 16);
	bool in_class = false;
	for (std::size_t i = 0; i < body.size(); ++i)
	{
		const char32_t ch = body[i];
		if (ch == U'\\' && i + 1 < body.size())
		{
			const char32_t esc = body[++i];
			switch (esc)
			{
			case U'w':
				out += in_class ? U"0-9A-Za-zА-Яа-яЁё" : U"[0-9A-Za-zА-Яа-яЁё]";
				break;
			case U'W':
				if (in_class)
					info.warnings.push_back("\\W внутри класса не переводится");
				else
					out += U"[^0-9A-Za-zА-Яа-яЁё]";
				break;
			case U's':
				out += in_class ? U" \\t\\n\\r\\f\\x{A0}" : U"[ \\t\\n\\r\\f\\x{A0}]";
				break;
			case U'S':
				if (in_class)
					info.warnings.push_back("\\S внутри класса не переводится");
				else
					out += U"[^ \\t\\n\\r\\f\\x{A0}]";
				break;
			case U'd':
				out += in_class ? U"0-9" : U"[0-9]";
				break;
			case U'D':
				if (in_class)
					info.warnings.push_back("\\D внутри класса не переводится");
				else
					out += U"[^0-9]";
				break;
			case U'b':
			case U'B':
				// RE2: \b/\B — по ASCII-слову, а не по WordChars оригинала.
				info.warnings.push_back("\\b внутри шаблона: границы по ASCII-слову");
				out += U"\\";
				out.push_back(esc);
				break;
			default:
				out += U"\\";
				out.push_back(esc);
				break;
			}
			continue;
		}
		if (ch == U'[')
			in_class = true;
		else if (ch == U']')
			in_class = false;
		out.push_back(ch);
	}
	return to_utf8(out);
}

std::expected<RegexDictionary, std::string> RegexDictionary::load(const std::filesystem::path& path,
																  std::vector<LoadIssue>* issues)
{
	std::ifstream in(path, std::ios::binary);
	if (!in)
		return std::unexpected("не удалось открыть словарь РВ: " + path.string());

	RegexDictionary dict;
	std::string line;
	auto add_issue = [&](std::string kind, std::string detail)
	{
		if (issues != nullptr)
			issues->push_back({ std::move(kind), std::move(detail) });
	};

	while (std::getline(in, line))
	{
		if (!line.empty() && line.back() == '\r')
			line.pop_back();
		if (line.empty() || line.front() == '#')
		{
			if (dict.version_.empty())
			{
				const std::string marker = cp1251::encode(U"Версия:");
				const auto p = line.find(marker);
				if (p != std::string::npos)
				{
					std::string v = line.substr(p + marker.size());
					while (!v.empty() && (v.front() == ' ' || v.front() == '\t'))
						v.erase(v.begin());
					while (!v.empty() && (v.back() == ' ' || v.back() == '\t'))
						v.pop_back();
					dict.version_ = v;
				}
			}
			continue;
		}
		if (line.find("\\0") != std::string::npos)
		{
			add_issue("old_format", line);
			continue;
		}

		RegexRule rule;
		rule.raw = cp1251::decode(line);
		rule.valid = make_valid_regexp(rule.raw);
		RuleBuildInfo info;
		rule.pattern = translate_to_re2(rule.valid, info);
		rule.lead_boundary = info.lead_boundary;
		rule.trail_boundary = info.trail_boundary;
		rule.ignore_case = !cyr::is_any_upper(rule.raw);
		for (std::size_t i = 0; i < rule.raw.size(); ++i)
			if (cyr::is_ye_yo(rule.raw[i]))
				rule.ye_yo.push_back(i);

		re2::RE2::Options opts;
		opts.set_log_errors(false);
		opts.set_case_sensitive(!rule.ignore_case);
		opts.set_dot_nl(true);
		opts.set_one_line(false);
		rule.re = std::make_unique<re2::RE2>(rule.pattern, opts);
		if (!rule.re->ok())
		{
			add_issue("bad_regex", line);
			continue;
		}
		for (auto& w : info.warnings)
			add_issue("translator", line + ": " + w);
		dict.rules_.push_back(std::move(rule));
	}
	dict.build_set();
	return dict;
}

void RegexDictionary::build_set()
{
	set_.reset();
	set_ids_.clear();
	if (rules_.empty())
		return;
	re2::RE2::Options opts;
	opts.set_log_errors(false);
	opts.set_dot_nl(true);
	opts.set_one_line(false);
	opts.set_case_sensitive(true);
	set_ = std::make_unique<re2::RE2::Set>(opts, re2::RE2::UNANCHORED);
	for (std::size_t i = 0; i < rules_.size(); ++i)
	{
		// Регистр в Set не важен (предфильтр), поэтому шаблон компилируется
		// с (?i) при ignore_case, чтобы не терять кандидатов.
		std::string pattern = rules_[i].ignore_case ? ("(?i)" + rules_[i].pattern)
													: rules_[i].pattern;
		if (set_->Add(pattern, nullptr) >= 0)
			set_ids_.push_back(i);
	}
	if (!set_->Compile())
	{
		// Предфильтр — оптимизация: при ошибке компиляции просто выключаем его.
		set_.reset();
		set_ids_.clear();
	}
}

void RegexDictionary::fill_utf8_map(ContextUtf8& out, UStringView context)
{
	out.bytes.clear();
	out.byte_to_cp.clear();
	out.bytes.reserve(context.size());
	out.byte_to_cp.reserve(context.size() + 1);
	for (std::size_t i = 0; i < context.size(); ++i)
	{
		const std::size_t before = out.bytes.size();
		append_utf8(out.bytes, context[i]);
		for (std::size_t k = before; k < out.bytes.size(); ++k)
			out.byte_to_cp.push_back(static_cast<std::uint32_t>(i));
	}
	out.byte_to_cp.push_back(static_cast<std::uint32_t>(context.size()));
}

int RegexDictionary::match_rule_utf8(const RegexRule& rule, UStringView context,
									 const ContextUtf8& ctx8, std::size_t di, UStringView word1,
									 UStringView word2) const
{
	const re2::StringPiece text(ctx8.bytes.data(), ctx8.bytes.size());
	const int ngroups = rule.re->NumberOfCapturingGroups() + 1;
	std::array<re2::StringPiece, 16> subs{};
	const int ngroups_used = std::min(ngroups, static_cast<int>(subs.size()));

	UString original(context);
	UString ctx(context);
	std::size_t cursor = 0;
	const std::size_t word_end = di + word1.size();

	while (cursor <= ctx8.bytes.size())
	{
		if (!rule.re->Match(text, cursor, ctx8.bytes.size(), re2::RE2::UNANCHORED, subs.data(),
							ngroups_used))
			break;
		const auto mstart = static_cast<std::size_t>(subs[0].data() - text.data());
		const auto mend = mstart + subs[0].size();
		cursor = mstart + 1;

		const auto cp_start = ctx8.byte_to_cp[std::min(mstart, ctx8.byte_to_cp.size() - 1)];
		const auto cp_end = ctx8.byte_to_cp[std::min(mend, ctx8.byte_to_cp.size() - 1)];
		if (rule.lead_boundary && cp_start > 0 && cyr::is_word_char(context[cp_start - 1]))
			continue;
		if (rule.trail_boundary && cp_end < context.size() && cyr::is_word_char(context[cp_end]))
			continue;

		bool is_new_word = false;
		std::size_t j = 0;
		for (int g = 1; g < ngroups_used; ++g)
		{
			const auto& sub = subs[static_cast<std::size_t>(g)];
			if (sub.data() == nullptr)
				continue;
			const auto gb = static_cast<std::size_t>(sub.data() - text.data());
			const auto ge = gb + sub.size();
			const auto gcp_b = ctx8.byte_to_cp[std::min(gb, ctx8.byte_to_cp.size() - 1)];
			const auto gcp_e = ctx8.byte_to_cp[std::min(ge, ctx8.byte_to_cp.size() - 1)];
			if (gcp_e - gcp_b != 1 || !cyr::is_ye_yo(ctx[gcp_b]))
				continue;
			if (j < rule.ye_yo.size())
				ctx[gcp_b] = rule.raw[rule.ye_yo[j]];
			++j;
			if (gcp_b >= di && gcp_b < word_end)
				is_new_word = true;
		}

		ctx = cyr::copy_case(ctx, original);
		if (!is_new_word)
			continue;
		const UString new_word = ctx.substr(di, word1.size());
		if (new_word == word1)
			return 1;
		if (new_word == word2)
			return 2;
	}
	return 0;
}

int RegexDictionary::match_rule(const RegexRule& rule, UStringView context, std::size_t di,
								UStringView word1, UStringView word2) const
{
	if (!rule.re || !rule.re->ok() || context.empty())
		return 0;
	ContextUtf8 ctx8;
	fill_utf8_map(ctx8, context);
	return match_rule_utf8(rule, context, ctx8, di, word1, word2);
}

bool RegexDictionary::lookup(UStringView context, std::size_t di, UStringView word1,
							 UStringView word2, int& out_word_n, std::size_t& out_rule) const
{
	out_word_n = 0;
	if (rules_.empty() || context.empty())
		return false;

	// Буферы переиспользуются между вызовами: в горячем пути нет аллокаций.
	thread_local ContextUtf8 ctx8;
	fill_utf8_map(ctx8, context);
	thread_local std::vector<int> candidates;
	candidates.clear();

	// Предфильтр: только правила, вообще способные совпасть на контексте.
	if (set_ != nullptr)
	{
		if (!set_->Match(ctx8.bytes, &candidates) || candidates.empty())
			return false;
		std::ranges::sort(candidates);
	}

	auto try_rule = [&](std::size_t idx) -> bool
	{
		if (!rules_[idx].re || !rules_[idx].re->ok())
			return false;
		const int n = match_rule_utf8(rules_[idx], context, ctx8, di, word1, word2);
		if (n == 0)
			return false;
		out_word_n = n;
		out_rule = idx;
		return true;
	};

	if (set_ == nullptr)
	{
		for (std::size_t i = 0; i < rules_.size(); ++i)
			if (try_rule(i))
				return true;
		return false;
	}
	for (int id : candidates)
	{
		if (id < 0 || static_cast<std::size_t>(id) >= set_ids_.size())
			continue;
		if (try_rule(set_ids_[static_cast<std::size_t>(id)]))
			return true;
	}
	return false;
}

} // namespace yo
