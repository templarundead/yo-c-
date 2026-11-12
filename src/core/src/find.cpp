#include "yo/find.hpp"

#include "yo/codec.hpp"
#include "yo/cyr.hpp"

#include <re2/re2.h>

#include <algorithm>
#include <memory>
#include <utility>
#include <cstdint>
#include <string>
#include <vector>

namespace yo {

namespace {

constexpr std::size_t kNpos = std::string::npos;

UString decode_utf8(std::string_view text)
{
	return decode_line({ reinterpret_cast<const std::uint8_t*>(text.data()), text.size() },
					   Encoding::utf8);
}

// Байтовые смещения начала каждой кодовой точки строки (+ конец строки).
std::vector<std::size_t> codepoint_offsets(std::string_view text)
{
	std::vector<std::size_t> offsets;
	offsets.reserve(text.size() + 1);
	for (std::size_t i = 0; i < text.size(); ++i)
	{
		if ((static_cast<unsigned char>(text[i]) & 0xC0U) != 0x80U)
			offsets.push_back(i);
	}
	offsets.push_back(text.size());
	return offsets;
}

bool is_word_char(char32_t c)
{
	return cyr::is_alpha(c) || cyr::is_ascii_digit(c) || c == U'_';
}

// Все вхождения образца в строке (байтовые смещения от начала строки).
std::vector<std::size_t> matches_in(std::string_view line, std::string_view needle, const FindOptions& opts)
{
	std::vector<std::size_t> out;
	if (needle.empty())
		return out;

	if (opts.case_sensitive && !opts.whole_word)
	{
		for (std::size_t at = line.find(needle); at != kNpos; at = line.find(needle, at + 1))
			out.push_back(at);
		return out;
	}

	const UString text = decode_utf8(line);
	const UString word = decode_utf8(needle);
	if (word.empty() || word.size() > text.size())
		return out;
	const std::vector<std::size_t> offsets = codepoint_offsets(line);

	const auto eq = [&](char32_t a, char32_t b)
	{ return opts.case_sensitive ? a == b : cyr::to_lower(a) == cyr::to_lower(b); };

	for (std::size_t i = 0; i + word.size() <= text.size(); ++i)
	{
		bool hit = true;
		for (std::size_t k = 0; k < word.size(); ++k)
		{
			if (!eq(text[i + k], word[k]))
			{
				hit = false;
				break;
			}
		}
		if (!hit)
			continue;
		if (opts.whole_word)
		{
			const bool left_ok = i == 0 || !is_word_char(text[i - 1]);
			const bool right_ok = i + word.size() >= text.size() || !is_word_char(text[i + word.size()]);
			if (!left_ok || !right_ok)
				continue;
		}
		out.push_back(offsets[i]);
	}
	return out;
}

} // namespace

std::vector<std::size_t> find_in_line(std::string_view line, std::string_view needle,
									  const FindOptions& opts)
{
	return matches_in(line, needle, opts);
}

std::size_t find_next(const TextBuffer& buf, std::size_t from, std::string_view needle,
					  const FindOptions& opts)
{
	if (needle.empty() || from >= buf.size())
		return kNpos;
	const std::size_t lines = buf.line_count();
	for (std::size_t line = buf.line_of(from); line < lines; ++line)
	{
		const std::string_view text = buf.line_text(line);
		const std::size_t base = buf.line_start(line);
		for (const std::size_t at : matches_in(text, needle, opts))
		{
			const std::size_t abs = base + at;
			if (abs >= from && abs + needle.size() <= buf.size())
				return abs;
		}
	}
	return kNpos;
}

std::size_t find_prev(const TextBuffer& buf, std::size_t from, std::string_view needle,
					  const FindOptions& opts)
{
	if (needle.empty() || buf.size() == 0)
		return kNpos;
	std::size_t line = from >= buf.size() ? buf.line_count() - 1 : buf.line_of(from);
	for (;;)
	{
		const std::string_view text = buf.line_text(line);
		const std::size_t base = buf.line_start(line);
		std::size_t best = kNpos;
		for (const std::size_t at : matches_in(text, needle, opts))
		{
			const std::size_t abs = base + at;
			if (abs < from)
				best = abs;
		}
		if (best != kNpos)
			return best;
		if (line == 0)
			break;
		--line;
	}
	return kNpos;
}

std::vector<std::size_t> find_all(const TextBuffer& buf, std::size_t from, std::size_t to,
								  std::string_view needle, const FindOptions& opts, std::size_t max_hits)
{
	std::vector<std::size_t> out;
	if (needle.empty() || from >= to || from >= buf.size())
		return out;
	to = std::min(to, buf.size());
	const std::size_t last_line = buf.line_of(to - 1);
	for (std::size_t line = buf.line_of(from); line <= last_line && line < buf.line_count(); ++line)
	{
		const std::string_view text = buf.line_text(line);
		const std::size_t base = buf.line_start(line);
		for (const std::size_t at : matches_in(text, needle, opts))
		{
			const std::size_t abs = base + at;
			if (abs < from || abs >= to)
				continue;
			out.push_back(abs);
			if (out.size() >= max_hits)
				return out;
		}
	}
	return out;
}

namespace {

using RegexPtr = std::unique_ptr<re2::RE2>;

// Общий разбор выражения: nullptr + error — образец не разобран.
RegexPtr make_regex(std::string_view pattern, bool case_sensitive, std::string& error)
{
	if (pattern.empty())
	{
		error = "пустой образец";
		return nullptr;
	}
	re2::RE2::Options opt;
	opt.set_case_sensitive(case_sensitive);
	opt.set_log_errors(false);
	opt.set_encoding(re2::RE2::Options::EncodingUTF8);
	auto re = std::make_unique<re2::RE2>(std::string(pattern), opt);
	if (!re->ok())
	{
		error = re->error();
		return nullptr;
	}
	return re;
}

// Первое совпадение внутри строки начиная с байта skip.
bool match_in_line(const re2::RE2& re, std::string_view text, std::size_t skip, std::size_t& begin,
				   std::size_t& end)
{
	re2::StringPiece m;
	if (!re.Match(re2::StringPiece(text.data(), text.size()), skip, text.size(), re2::RE2::UNANCHORED, &m,
				  1))
		return false;
	begin = static_cast<std::size_t>(m.data() - text.data());
	end = begin + m.size();
	return true;
}

} // namespace

FindRegexResult find_regex(const TextBuffer& buf, std::size_t from, std::string_view pattern,
						   bool case_sensitive)
{
	FindRegexResult res;
	const RegexPtr re = make_regex(pattern, case_sensitive, res.error);
	if (re == nullptr)
		return res;
	if (buf.size() == 0)
		return res;
	if (from > buf.size())
		from = buf.size();
	const std::size_t lines = buf.line_count();
	for (std::size_t line = buf.line_of(from); line < lines; ++line)
	{
		const std::string_view text = buf.line_text(line);
		const std::size_t base = buf.line_start(line);
		std::size_t skip = from > base ? from - base : 0;
		// Срез может попасть внутрь символа — сдвигаем к его началу.
		while (skip > 0 && skip < text.size() &&
			   (static_cast<unsigned char>(text[skip]) & 0xC0U) == 0x80U)
			++skip;
		if (skip > text.size())
			continue;
		std::size_t a = 0;
		std::size_t b = 0;
		if (match_in_line(*re, text, skip, a, b))
		{
			res.begin = base + a;
			res.end = base + b;
			return res;
		}
	}
	return res;
}

FindRegexResult find_regex_prev(const TextBuffer& buf, std::size_t before, std::string_view pattern,
								bool case_sensitive)
{
	FindRegexResult res;
	const RegexPtr re = make_regex(pattern, case_sensitive, res.error);
	if (re == nullptr)
		return res;
	if (buf.size() == 0 || before == 0)
		return res;
	before = std::min(before, buf.size());
	const std::size_t lines = buf.line_count();
	if (lines == 0)
		return res;
	const std::size_t last_line = std::min(buf.line_of(before - 1), lines - 1);
	for (std::size_t i = last_line + 1; i > 0; --i)
	{
		const std::size_t line = i - 1;
		const std::string_view text = buf.line_text(line);
		const std::size_t base = buf.line_start(line);
		std::size_t a = 0;
		std::size_t b = 0;
		std::size_t skip = 0;
		while (skip <= text.size() && match_in_line(*re, text, skip, a, b))
		{
			if (base + a >= before)
				break;
			res.begin = base + a;
			res.end = base + b;
			// Нулевая длина — иначе зациклимся.
			skip = b > skip ? b : skip + 1;
		}
		if (res.found())
			return res;
	}
	return res;
}

std::size_t replace_all(TextBuffer& buf, std::size_t from, std::size_t to, std::string_view needle,
						std::string_view replacement, const FindOptions& opts)
{
	const std::vector<std::size_t> hits = find_all(buf, from, to, needle, opts);
	if (hits.empty())
		return 0;
	buf.begin_group("Заменить всё");
	// С конца: смещения предыдущих вхождений остаются верными.
	for (std::size_t i = hits.size(); i > 0; --i)
	{
		const std::size_t at = hits[i - 1];
		buf.erase(at, needle.size());
		if (!replacement.empty())
			buf.insert(at, replacement);
	}
	buf.end_group();
	return hits.size();
}

} // namespace yo
