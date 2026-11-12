#include "yo/ini.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <string>

namespace yo {

std::string_view ini_trim(std::string_view text)
{
	std::size_t begin = 0;
	while (begin < text.size() && (text[begin] == ' ' || text[begin] == '\t' || text[begin] == '\r'))
		++begin;
	std::size_t end = text.size();
	while (end > begin && (text[end - 1] == ' ' || text[end - 1] == '\t' || text[end - 1] == '\r'))
		--end;
	return text.substr(begin, end - begin);
}

namespace {

std::string lower(std::string_view text)
{
	std::string out(text);
	for (char& c : out)
	{
		if (c >= 'A' && c <= 'Z')
			c = static_cast<char>(c - 'A' + 'a');
	}
	return out;
}

} // namespace

void Ini::parse(std::string_view text)
{
	entries_.clear();
	std::string section;
	std::size_t begin = 0;
	while (begin <= text.size())
	{
		const std::size_t end = text.find('\n', begin);
		std::string_view line =
			end == std::string_view::npos ? text.substr(begin) : text.substr(begin, end - begin);
		begin = end == std::string_view::npos ? text.size() + 1 : end + 1;

		line = ini_trim(line);
		if (line.empty() || line.front() == ';' || line.front() == '#')
			continue;
		if (line.front() == '[')
		{
			const std::size_t close = line.find(']');
			if (close == std::string_view::npos)
				continue; // битая секция — пропускаем
			section = std::string(ini_trim(line.substr(1, close - 1)));
			continue;
		}
		const std::size_t eq = line.find('=');
		if (eq == std::string_view::npos)
			continue; // строка без «=» — не наша
		const std::string key(ini_trim(line.substr(0, eq)));
		if (key.empty())
			continue;
		entries_.push_back({ section, key, std::string(ini_trim(line.substr(eq + 1))) });
	}
}

std::string Ini::dump() const
{
	std::string out;
	std::string current;
	bool first_section = true;
	for (const Entry& e : entries_)
	{
		if (e.section != current)
		{
			if (!first_section)
				out += '\n';
			first_section = false;
			current = e.section;
			if (!current.empty())
			{
				out += '[';
				out += current;
				out += "]\n";
			}
		}
		out += e.key;
		out += '=';
		out += e.value;
		out += '\n';
	}
	return out;
}

bool Ini::has(std::string_view section, std::string_view key) const
{
	return std::any_of(entries_.begin(), entries_.end(), [&](const Entry& e)
					   { return e.section == section && lower(e.key) == lower(key); });
}

std::string Ini::get(std::string_view section, std::string_view key, std::string_view fallback) const
{
	const std::string want = lower(key);
	for (const Entry& e : entries_)
	{
		if (e.section == section && lower(e.key) == want)
			return e.value;
	}
	return std::string(fallback);
}

long long Ini::get_int(std::string_view section, std::string_view key, long long fallback) const
{
	const std::string text = get(section, key);
	if (text.empty())
		return fallback;
	try
	{
		std::size_t used = 0;
		const long long value = std::stoll(text, &used);
		// Хвост после числа («12px») считаем мусором: лучше значение
		// по умолчанию, чем случайное число.
		if (used == 0 || !ini_trim(std::string_view(text).substr(used)).empty())
			return fallback;
		return value;
	}
	catch (...)
	{
		return fallback; // «12px» и прочий мусор — берём значение по умолчанию
	}
}

float Ini::get_float(std::string_view section, std::string_view key, float fallback) const
{
	const std::string text = get(section, key);
	if (text.empty())
		return fallback;
	try
	{
		std::size_t used = 0;
		const float value = std::stof(text, &used);
		if (used == 0 || !ini_trim(std::string_view(text).substr(used)).empty())
			return fallback;
		return value;
	}
	catch (...)
	{
		return fallback;
	}
}

bool Ini::get_bool(std::string_view section, std::string_view key, bool fallback) const
{
	std::string text = lower(get(section, key));
	if (text.empty())
		return fallback;
	if (text == "1" || text == "true" || text == "yes" || text == "on" || text == "да")
		return true;
	if (text == "0" || text == "false" || text == "no" || text == "off" || text == "нет")
		return false;
	return fallback;
}

void Ini::set(std::string_view section, std::string_view key, std::string value)
{
	// Переводы строк в значении сломали бы формат — заменяем пробелом.
	for (char& c : value)
	{
		if (c == '\n' || c == '\r')
			c = ' ';
	}
	const std::string want = lower(key);
	for (Entry& e : entries_)
	{
		if (e.section == section && lower(e.key) == want)
		{
			e.value = std::move(value);
			return;
		}
	}
	entries_.push_back({ std::string(section), std::string(key), std::move(value) });
}

void Ini::set_int(std::string_view section, std::string_view key, long long value)
{
	set(section, key, std::to_string(value));
}

void Ini::set_float(std::string_view section, std::string_view key, float value)
{
	char buf[32] = {};
	std::snprintf(buf, sizeof(buf), "%.2f", static_cast<double>(value));
	// Убираем хвостовые нули: 15.00 → 15.
	std::string text(buf);
	while (text.size() > 1 && text.back() == '0' && text.find('.') != std::string::npos)
		text.pop_back();
	if (!text.empty() && text.back() == '.')
		text.pop_back();
	set(section, key, std::move(text));
}

void Ini::set_bool(std::string_view section, std::string_view key, bool value)
{
	set(section, key, value ? "1" : "0");
}

} // namespace yo
