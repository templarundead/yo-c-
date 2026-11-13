#include "yo/usersel.hpp"

namespace yo {

namespace {

std::string_view trim(std::string_view text)
{
	while (!text.empty() && (text.front() == ' ' || text.front() == '\t' || text.front() == '\r'))
		text.remove_prefix(1);
	while (!text.empty() && (text.back() == ' ' || text.back() == '\t' || text.back() == '\r'))
		text.remove_suffix(1);
	return text;
}

} // namespace

std::vector<UserSelEntry> parse_user_selections(std::string_view text)
{
	std::vector<UserSelEntry> out;
	std::size_t pos = 0;
	while (pos <= text.size() && out.size() < kUserSelMax)
	{
		const std::size_t eol = text.find('\n', pos);
		const std::string_view line =
			trim(text.substr(pos, eol == std::string_view::npos ? std::string_view::npos : eol - pos));
		pos = eol == std::string_view::npos ? text.size() + 1 : eol + 1;
		if (line.empty() || line.front() == ';' || line.front() == '#')
			continue;
		const std::size_t tab = line.find_last_of('\t');
		if (tab == std::string_view::npos)
			continue;
		const std::string_view word = trim(line.substr(0, tab));
		const std::string_view value = trim(line.substr(tab + 1));
		if (word.empty() || value.size() != 1)
			continue;
		if (value.front() != '1' && value.front() != '2')
			continue; // 0 — «не запомнено», остальное — мусор
		out.push_back(UserSelEntry{ std::string(word), value.front() == '1' ? 1 : 2 });
	}
	return out;
}

std::string format_user_selections(const std::vector<UserSelEntry>& entries)
{
	std::string out;
	out.reserve(entries.size() * 16);
	for (const UserSelEntry& e : entries)
	{
		if (e.word.empty() || (e.value != 1 && e.value != 2))
			continue;
		out += e.word;
		out += '\t';
		out += e.value == 1 ? '1' : '2';
		out += '\n';
	}
	return out;
}

} // namespace yo
