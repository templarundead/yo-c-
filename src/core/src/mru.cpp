#include "yo/mru.hpp"

#include "yo/cyr.hpp"

#include <algorithm>

namespace yo {

namespace {

bool is_flag_char(char c)
{
	return c == 'u' || c == 'U' || c == 'S' || c == 'R';
}

// «12» → 12; мусор и отрицательные — 0 (как StrToInt с проверкой в оригинале).
std::size_t parse_position(std::string_view text)
{
	std::size_t value = 0;
	bool digits = false;
	for (const char c : text)
	{
		if (c < '0' || c > '9')
			return 0;
		value = value * 10 + static_cast<std::size_t>(c - '0');
		digits = true;
	}
	return digits ? value : 0;
}

char32_t lower(char32_t c) { return cyr::to_lower(c); }

// Одна кодовая точка UTF-8; мусорные байты читаются как есть.
char32_t next_cp(std::string_view text, std::size_t& i)
{
	const auto c0 = static_cast<unsigned char>(text[i]);
	if (c0 < 0x80U)
	{
		++i;
		return static_cast<char32_t>(c0);
	}
	const auto cont = [&](std::size_t k) { return k < text.size() &&
		(static_cast<unsigned char>(text[k]) & 0xC0U) == 0x80U; };
	if ((c0 & 0xE0U) == 0xC0U && cont(i + 1))
	{
		const char32_t cp = ((c0 & 0x1FU) << 6) |
			(static_cast<unsigned char>(text[i + 1]) & 0x3FU);
		i += 2;
		return cp;
	}
	if ((c0 & 0xF0U) == 0xE0U && cont(i + 1) && cont(i + 2))
	{
		const char32_t cp = ((c0 & 0x0FU) << 12) |
			((static_cast<unsigned char>(text[i + 1]) & 0x3FU) << 6) |
			(static_cast<unsigned char>(text[i + 2]) & 0x3FU);
		i += 3;
		return cp;
	}
	if ((c0 & 0xF8U) == 0xF0U && cont(i + 1) && cont(i + 2) && cont(i + 3))
	{
		const char32_t cp = ((c0 & 0x07U) << 18) |
			((static_cast<unsigned char>(text[i + 1]) & 0x3FU) << 12) |
			((static_cast<unsigned char>(text[i + 2]) & 0x3FU) << 6) |
			(static_cast<unsigned char>(text[i + 3]) & 0x3FU);
		i += 4;
		return cp;
	}
	return static_cast<char32_t>(static_cast<unsigned char>(text[i++]));
}

} // namespace

MruEntry mru_parse(std::string_view value)
{
	MruEntry entry;
	std::string_view rest = value;
	// С конца: если после последней запятой только флаги — это флаги.
	if (const std::size_t last = rest.rfind(','); last != std::string_view::npos)
	{
		const std::string_view tail = rest.substr(last + 1);
		if (!tail.empty() && std::all_of(tail.begin(), tail.end(), is_flag_char))
		{
			entry.flags = std::string(tail);
			rest = rest.substr(0, last);
		}
	}
	// Затем — позиция.
	if (const std::size_t last = rest.rfind(','); last != std::string_view::npos)
	{
		entry.position = parse_position(rest.substr(last + 1));
		rest = rest.substr(0, last);
	}
	entry.path = std::string(rest);
	return entry;
}

std::string mru_format(const MruEntry& entry)
{
	std::string out = entry.path;
	out += ',';
	out += std::to_string(entry.position);
	if (!entry.flags.empty())
	{
		out += ',';
		out += entry.flags;
	}
	return out;
}

bool mru_same_path(std::string_view a, std::string_view b)
{
	// В оригинале сравнение — `LC` (AnsiLowerCase), то есть без учёта
	// регистра; сравниваем посимвольно с приведением кириллицы и латиницы.
	std::size_t i = 0;
	std::size_t j = 0;
	while (i < a.size() && j < b.size())
	{
		const char32_t ca = next_cp(a, i);
		const char32_t cb = next_cp(b, j);
		if (lower(ca) != lower(cb))
			return false;
	}
	return i >= a.size() && j >= b.size();
}

std::size_t MruList::add(MruEntry entry)
{
	if (const std::size_t at = find(entry.path); at < entries_.size())
	{
		// Файл уже в списке: порядок не меняем (FIFO, как в оригинале) и
		// запомненную позицию курсора не затираем — для этого есть remember().
		return at;
	}
	entries_.push_back(std::move(entry));
	if (entries_.size() > kMruMax)
		entries_.erase(entries_.begin());
	return entries_.size() - 1;
}

void MruList::remember(std::string_view path, std::size_t position, std::string flags)
{
	if (const std::size_t at = find(path); at < entries_.size())
	{
		entries_[at].position = position;
		if (!flags.empty())
			entries_[at].flags = std::move(flags);
	}
}

void MruList::remove(std::string_view path)
{
	const std::size_t at = find(path);
	if (at < entries_.size())
		entries_.erase(entries_.begin() + static_cast<std::ptrdiff_t>(at));
}

void MruList::clear() { entries_.clear(); }

void MruList::set(const std::vector<MruEntry>& entries)
{
	entries_.clear();
	for (const MruEntry& e : entries)
	{
		if (e.path.empty() || entries_.size() >= kMruMax)
			continue;
		if (find(e.path) < entries_.size())
			continue; // дубликат в файле настроек — оставляем первый
		entries_.push_back(e);
	}
}

std::size_t MruList::find(std::string_view path) const
{
	for (std::size_t i = 0; i < entries_.size(); ++i)
	{
		if (mru_same_path(entries_[i].path, path))
			return i;
	}
	return kMruMax;
}

const MruEntry* MruList::at(std::size_t index) const
{
	return index < entries_.size() ? &entries_[index] : nullptr;
}

} // namespace yo
