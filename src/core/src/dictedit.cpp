#include "yo/dictedit.hpp"

#include "yo/cp1251.hpp"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iterator>

namespace yo {

namespace {

// Trim по пробелам/табам (как GetDicVersion).
std::string trim(std::string_view s)
{
	std::size_t b = 0;
	std::size_t e = s.size();
	while (b < e && (s[b] == ' ' || s[b] == '\t' || s[b] == '\r' || s[b] == '\n'))
		++b;
	while (e > b && (s[e - 1] == ' ' || s[e - 1] == '\t' || s[e - 1] == '\r' || s[e - 1] == '\n'))
		--e;
	return std::string(s.substr(b, e - b));
}

// Строки CP1251-текста без CR.
std::vector<std::string> split_lines(std::string_view text)
{
	std::vector<std::string> out;
	std::size_t start = 0;
	while (start <= text.size())
	{
		const std::size_t nl = text.find('\n', start);
		const std::size_t end = nl == std::string_view::npos ? text.size() : nl;
		std::string line(text.substr(start, end - start));
		if (!line.empty() && line.back() == '\r')
			line.pop_back();
		out.push_back(std::move(line));
		if (nl == std::string_view::npos)
			break;
		start = nl + 1;
	}
	// Финальный перевод строки не создаёт лишнюю строку.
	if (!out.empty() && out.back().empty() && !text.empty() && text.back() == '\n')
		out.pop_back();
	return out;
}

bool is_comment(std::string_view line)
{
	return line.empty() || line.front() == '#';
}

std::string version_from(std::string_view line)
{
	const std::string marker = cp1251::encode(U"Версия:");
	const std::size_t p = line.find(marker);
	if (p == std::string::npos)
		return {};
	return trim(line.substr(p + marker.size()));
}

// Ключ сравнения оригинала: CmpYeEx — форма с «е» в CP1251 и её LC.
struct SortKey
{
	std::string ye;
	std::string lc;
};

SortKey sort_key(UStringView yo)
{
	SortKey k;
	k.ye = cp1251::encode(cyr::to_ye(yo));
	k.lc = cp1251::lower_key(k.ye);
	return k;
}

// Cmp оригинала: сначала LC, затем побайтовое сравнение сырых форм.
int cmp_keys(const SortKey& a, const SortKey& b)
{
	const int c = a.lc.compare(b.lc);
	if (c != 0)
		return c < 0 ? -1 : 1;
	const int r = a.ye.compare(b.ye);
	return r < 0 ? -1 : (r > 0 ? 1 : 0);
}

std::string to_utf8(UStringView s)
{
	std::string out;
	out.reserve(s.size() * 2);
	for (char32_t c : s)
	{
		if (c < 0x80)
		{
			out.push_back(static_cast<char>(c));
		}
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
	return out;
}

// Форма с «ё» для тестового поля: если «ё» нет — первое «е» становится «ё»
// (это ровно тот случай, который проверяет словарь РВ).
UString to_yo_form(UStringView word)
{
	UString out(word);
	for (char32_t& c : out)
	{
		if (c == cyr::kYeLower)
		{
			c = cyr::kYoLower;
			return out;
		}
		if (c == cyr::kYeUpper)
		{
			c = cyr::kYoUpper;
			return out;
		}
	}
	return out;
}

// Запись файла: временный рядом + rename (атомарная замена, как сохранение
// документа в GUI и --in-place в CLI).
bool write_atomic(const std::string& path, const std::string& bytes, std::string& error)
{
	if (path.empty())
	{
		error = "путь словаря не задан";
		return false;
	}
	const std::string tmp = path + ".yo-tmp";
	{
		std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
		if (!out)
		{
			error = "не удалось записать " + tmp;
			return false;
		}
		out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
		if (!out)
		{
			error = "ошибка записи " + tmp;
			return false;
		}
	}
	std::error_code ec;
	std::filesystem::rename(tmp, path, ec);
	if (ec)
	{
		std::filesystem::remove(tmp, ec);
		error = "не удалось заменить " + path;
		return false;
	}
	return true;
}

std::string read_file(const std::string& path)
{
	std::ifstream in(path, std::ios::binary);
	if (!in)
		return {};
	return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

} // namespace

// ---------------------------------------------------------------- словарь

WordCheck check_dict_word(UStringView yo)
{
	if (yo.empty())
		return { WordProblem::empty, "слово пустое" };
	for (char32_t c : yo)
	{
		if (!cyr::is_alpha(c))
		{
			return { WordProblem::bad_chars,
					 "недопустимые символы: только русские буквы (можно «ё»/«Ё»)" };
		}
	}
	if (!cyr::is_yo_in(yo))
		return { WordProblem::no_yo, "в слове нет «ё»: словарь хранит формы с ё" };
	return {};
}

DictFile DictFile::parse(std::string_view cp1251_text)
{
	DictFile file;
	for (const std::string& line : split_lines(cp1251_text))
	{
		if (is_comment(line))
		{
			if (file.version.empty())
			{
				const std::string v = version_from(line);
				if (!v.empty())
					file.version = v;
			}
			file.header.push_back(cp1251::decode(line));
			continue;
		}
		DictLine w;
		std::string yo = line;
		if (!yo.empty() && yo.back() == '?')
		{
			yo.pop_back();
			w.ask = true;
		}
		w.yo = cp1251::decode(yo);
		file.words.push_back(std::move(w));
	}
	return file;
}

std::string DictFile::format() const
{
	std::string out;
	const auto put = [&out](UStringView line)
	{
		out += cp1251::encode(line);
		out += "\r\n";
	};
	for (const UString& h : header)
		put(h);
	for (const DictLine& w : words)
	{
		UString line = w.yo;
		if (w.ask)
			line.push_back(U'?');
		put(line);
	}
	return out;
}

DictOrder check_dict_order(const DictFile& file)
{
	DictOrder out;
	SortKey prev;
	for (std::size_t i = 0; i < file.words.size(); ++i)
	{
		const SortKey cur = sort_key(file.words[i].yo);
		if (i > 0 && cmp_keys(prev, cur) > 0)
		{
			out.sorted = false;
			out.index = i;
			out.word = to_utf8(file.words[i].yo);
			return out;
		}
		prev = cur;
	}
	return out;
}

DictEdit dict_insert(DictFile& file, UStringView yo, bool ask)
{
	const WordCheck check = check_dict_word(yo);
	if (!check.ok())
		return { false, 0, check.message };
	const std::size_t existing = dict_find_word(file, yo);
	if (existing != std::string::npos)
		return { false, existing, "такое слово уже есть: " + to_utf8(file.words[existing].yo) };

	DictLine line;
	line.yo = UString(yo);
	line.ask = ask;

	const SortKey key = sort_key(yo);
	std::size_t pos = 0;
	while (pos < file.words.size() && cmp_keys(sort_key(file.words[pos].yo), key) <= 0)
		++pos;
	file.words.insert(file.words.begin() + static_cast<std::ptrdiff_t>(pos), std::move(line));
	return { true, pos, "добавлено" };
}

DictEdit dict_replace(DictFile& file, std::size_t index, UStringView yo, bool ask)
{
	if (index >= file.words.size())
		return { false, 0, "строка не выбрана" };
	const WordCheck check = check_dict_word(yo);
	if (!check.ok())
		return { false, index, check.message };
	const std::size_t existing = dict_find_word(file, yo);
	if (existing != std::string::npos && existing != index)
		return { false, existing, "такое слово уже есть: " + to_utf8(file.words[existing].yo) };

	// Проще удалить и вставить заново: порядок восстанавливается сам.
	file.words.erase(file.words.begin() + static_cast<std::ptrdiff_t>(index));
	DictEdit res = dict_insert(file, yo, ask);
	if (res.ok)
		res.message = "изменено";
	return res;
}

bool dict_erase(DictFile& file, std::size_t index)
{
	if (index >= file.words.size())
		return false;
	file.words.erase(file.words.begin() + static_cast<std::ptrdiff_t>(index));
	return true;
}

std::size_t dict_find_word(const DictFile& file, UStringView word)
{
	const SortKey key = sort_key(word);
	for (std::size_t i = 0; i < file.words.size(); ++i)
	{
		if (cmp_keys(sort_key(file.words[i].yo), key) == 0)
			return i;
	}
	return std::string::npos;
}

void dict_sort(DictFile& file)
{
	std::stable_sort(file.words.begin(), file.words.end(),
					 [](const DictLine& a, const DictLine& b)
					 { return cmp_keys(sort_key(a.yo), sort_key(b.yo)) < 0; });
}

// Подстрока без учёта регистра и е/ё: «ёлка» находится и по «ел».
bool contains_folded(UStringView haystack, UStringView needle)
{
	if (needle.empty())
		return true;
	const UString h = cyr::to_lower(cyr::to_ye(haystack));
	const UString n = cyr::to_lower(cyr::to_ye(needle));
	return h.find(n) != UString::npos;
}

std::vector<std::size_t> dict_filter(const DictFile& file, const DictFilter& filter)
{
	std::vector<std::size_t> out;
	out.reserve(file.words.size());
	for (std::size_t i = 0; i < file.words.size(); ++i)
	{
		const DictLine& w = file.words[i];
		if (filter.only_ask && !w.ask)
			continue;
		if (!filter.query.empty() && !contains_folded(cyr::to_ye(w.yo), filter.query))
			continue;
		out.push_back(i);
	}
	return out;
}

// ---------------------------------------------------------------- правила РВ

RegexFile RegexFile::parse(std::string_view cp1251_text)
{
	RegexFile file;
	for (const std::string& raw : split_lines(cp1251_text))
	{
		RegexLine line;
		line.text = cp1251::decode(raw);
		line.rule = !is_comment(raw);
		if (!file.version.empty() || !line.rule)
		{
			if (file.version.empty())
			{
				const std::string v = version_from(raw);
				if (!v.empty())
					file.version = v;
			}
		}
		file.lines.push_back(std::move(line));
	}
	return file;
}

std::string RegexFile::format() const
{
	std::string out;
	for (const RegexLine& line : lines)
	{
		out += cp1251::encode(line.text);
		out += "\r\n";
	}
	return out;
}

std::vector<std::size_t> RegexFile::rule_lines() const
{
	std::vector<std::size_t> out;
	for (std::size_t i = 0; i < lines.size(); ++i)
	{
		if (lines[i].rule)
			out.push_back(i);
	}
	return out;
}

std::size_t RegexFile::rule_count() const
{
	return static_cast<std::size_t>(
		std::count_if(lines.begin(), lines.end(), [](const RegexLine& l) { return l.rule; }));
}

RuleCheck check_regex_rule(UStringView raw)
{
	RuleCheck out;
	const UString valid = make_valid_regexp(raw);
	RuleBuildInfo info;
	const std::string pattern = translate_to_re2(valid, info);
	out.valid = to_utf8(valid);
	out.pattern = pattern;

	re2::RE2::Options opts;
	opts.set_log_errors(false);
	opts.set_case_sensitive(!cyr::is_any_upper(raw));
	opts.set_dot_nl(true);
	opts.set_one_line(false);
	re2::RE2 re(pattern, opts);
	if (!re.ok())
	{
		out.error = re.error();
		if (out.error.empty())
			out.error = "правило не компилируется";
		return out;
	}
	out.ok = true;
	return out;
}

RegexProbe probe_regex(const RegexFile& file, UStringView context, std::size_t di, UStringView word)
{
	RegexProbe out;
	const std::string text = file.format();
	std::vector<LoadIssue> issues;
	auto dict = RegexDictionary::parse(text, &issues);
	if (!dict)
	{
		out.ok = false;
		out.error = dict.error();
		return out;
	}
	for (const LoadIssue& issue : issues)
	{
		if (issue.kind == "bad_regex")
		{
			out.ok = false;
			out.error = "не компилируется правило: " + issue.detail;
			break;
		}
	}

	const UString word1 = cyr::to_ye(word);
	const UString word2 = cyr::is_yo_in(word) ? UString(word) : to_yo_form(word);
	out.word_ye = to_utf8(word1);
	out.word_yo = to_utf8(word2);

	int word_n = 0;
	std::size_t rule_index = 0;
	if (!dict->lookup(context, di, word1, word2, word_n, rule_index))
		return out;

	// Индекс правила → индекс строки файла.
	const std::vector<std::size_t> lines = file.rule_lines();
	out.matched = true;
	out.rule_no = rule_index;
	out.word_n = word_n;
	out.line = rule_index < lines.size() ? lines[rule_index] : 0;
	return out;
}

// ---------------------------------------------------------------- состояние

std::size_t DictEditorState::selected_line() const
{
	if (cursor >= visible.size())
		return std::string::npos;
	return visible[cursor];
}

namespace {

void fail(DictEditorState& state, std::string message)
{
	state.message = std::move(message);
	state.message_error = true;
}

void done(DictEditorState& state, std::string message)
{
	state.message = std::move(message);
	state.message_error = false;
}

} // namespace

void dict_editor_refilter(DictEditorState& state)
{
	state.visible.clear();
	if (state.mode == DictMode::words)
	{
		state.visible = dict_filter(state.words, state.filter);
	}
	else
	{
		const std::vector<std::size_t> lines = state.rules.rule_lines();
		for (std::size_t i = 0; i < lines.size(); ++i)
		{
			const UString& text = state.rules.lines[lines[i]].text;
			if (!state.filter.query.empty() && !contains_folded(text, state.filter.query))
				continue;
			state.visible.push_back(i); // индекс строки правила, не файла
		}
	}
	if (state.visible.empty())
		state.cursor = 0;
	else if (state.cursor >= state.visible.size())
		state.cursor = state.visible.size() - 1;
	if (state.top > state.cursor)
		state.top = state.cursor;
}

void dict_editor_load(DictEditorState& state, std::string path_words, std::string path_rules)
{
	state.path_words = std::move(path_words);
	state.path_rules = std::move(path_rules);

	const std::string words_text = read_file(state.path_words);
	state.words = DictFile::parse(words_text);
	const std::string rules_text = read_file(state.path_rules);
	state.rules = RegexFile::parse(rules_text);

	state.dirty = false;
	state.cursor = 0;
	state.top = 0;
	state.edit.clear();
	state.edit_ask = false;
	state.field = DictField::filter;
	state.filter = {};
	dict_editor_refilter(state);

	const DictOrder order = check_dict_order(state.words);
	if (!order.sorted)
	{
		fail(state, "словарь не отсортирован: «" + order.word + "» (строка " +
					   std::to_string(order.index + 1) + ")");
		return;
	}
	done(state, "словарь: " + std::to_string(state.words.size()) + " слов, правил РВ: " +
				   std::to_string(state.rules.rule_count()));
}

void dict_editor_move(DictEditorState& state, long delta)
{
	if (state.visible.empty())
		return;
	long target = static_cast<long>(state.cursor) + delta;
	if (target < 0)
		target = 0;
	if (static_cast<std::size_t>(target) >= state.visible.size())
		target = static_cast<long>(state.visible.size()) - 1;
	state.cursor = static_cast<std::size_t>(target);
	if (state.cursor < state.top)
		state.top = state.cursor;
}

void dict_editor_pick(DictEditorState& state)
{
	if (state.visible.empty() || state.cursor >= state.visible.size())
		return;
	if (state.mode == DictMode::words)
	{
		const DictLine& w = state.words.words[state.visible[state.cursor]];
		state.edit = w.yo;
		state.edit_ask = w.ask;
	}
	else
	{
		const std::vector<std::size_t> lines = state.rules.rule_lines();
		if (state.cursor >= lines.size())
			return;
		state.edit = state.rules.lines[lines[state.cursor]].text;
		state.edit_ask = false;
	}
	state.field = DictField::text;
}

bool dict_editor_apply(DictEditorState& state)
{
	if (state.edit.empty())
	{
		fail(state, "введите словоформу с «ё»");
		return false;
	}
	if (state.mode == DictMode::words)
	{
		const std::size_t line = state.selected_line();
		if (line == std::string::npos)
		{
			fail(state, "строка не выбрана: «Изменить» правит выбранную строку");
			return false;
		}
		const DictEdit res = dict_replace(state.words, line, state.edit, state.edit_ask);
		if (!res.ok)
		{
			fail(state, res.message);
			return false;
		}
		state.dirty = true;
		dict_editor_refilter(state);
		for (std::size_t i = 0; i < state.visible.size(); ++i)
		{
			if (state.visible[i] == res.index)
			{
				state.cursor = i;
				break;
			}
		}
		done(state, "изменено: " + to_utf8(state.edit));
		return true;
	}

	// Правила РВ: поле — шаблон правила.
	const RuleCheck check = check_regex_rule(state.edit);
	if (!check.ok)
	{
		fail(state, "правило не компилируется: " + check.error);
		return false;
	}
	const std::vector<std::size_t> lines = state.rules.rule_lines();
	if (state.cursor >= lines.size())
	{
		fail(state, "правило не выбрано");
		return false;
	}
	state.rules.lines[lines[state.cursor]].text = state.edit;
	state.dirty = true;
	dict_editor_refilter(state);
	done(state, "правило изменено");
	return true;
}

bool dict_editor_add(DictEditorState& state)
{
	if (state.edit.empty())
	{
		fail(state, state.mode == DictMode::words ? "введите словоформу с «ё»"
												  : "введите шаблон правила");
		return false;
	}
	if (state.mode == DictMode::words)
	{
		const DictEdit res = dict_insert(state.words, state.edit, state.edit_ask);
		if (!res.ok)
		{
			fail(state, res.message);
			return false;
		}
		state.dirty = true;
		dict_editor_refilter(state);
		for (std::size_t i = 0; i < state.visible.size(); ++i)
		{
			if (state.visible[i] == res.index)
			{
				state.cursor = i;
				break;
			}
		}
		done(state, "добавлено: " + to_utf8(state.edit));
		return true;
	}

	const RuleCheck check = check_regex_rule(state.edit);
	if (!check.ok)
	{
		fail(state, "правило не компилируется: " + check.error);
		return false;
	}
	state.rules.lines.push_back(RegexLine{ state.edit, true });
	state.dirty = true;
	dict_editor_refilter(state);
	state.cursor = state.visible.empty() ? 0 : state.visible.size() - 1;
	done(state, "правило добавлено");
	return true;
}

bool dict_editor_erase(DictEditorState& state)
{
	if (state.visible.empty())
	{
		fail(state, "нечего удалять");
		return false;
	}
	if (state.mode == DictMode::words)
	{
		const std::size_t line = state.selected_line();
		if (line == std::string::npos || !dict_erase(state.words, line))
		{
			fail(state, "строка не выбрана");
			return false;
		}
		state.dirty = true;
		dict_editor_refilter(state);
		done(state, "удалено");
		return true;
	}
	const std::vector<std::size_t> lines = state.rules.rule_lines();
	if (state.cursor >= lines.size())
	{
		fail(state, "строка не выбрана");
		return false;
	}
	state.rules.lines.erase(state.rules.lines.begin() +
							static_cast<std::ptrdiff_t>(lines[state.cursor]));
	state.dirty = true;
	dict_editor_refilter(state);
	done(state, "правило удалено");
	return true;
}

bool dict_editor_save(DictEditorState& state)
{
	const DictOrder order = check_dict_order(state.words);
	if (!order.sorted)
	{
		fail(state, "порядок слов нарушен («" + order.word + "»), сохранение отменено");
		return false;
	}
	std::string error;
	if (!write_atomic(state.path_words, state.words.format(), error))
	{
		fail(state, error);
		return false;
	}
	if (!write_atomic(state.path_rules, state.rules.format(), error))
	{
		fail(state, error);
		return false;
	}
	state.dirty = false;
	done(state, "сохранено: " + std::to_string(state.words.size()) + " слов, " +
				   std::to_string(state.rules.rule_count()) + " правил");
	return true;
}

RegexProbe dict_editor_probe(const DictEditorState& state, UStringView context, UStringView word)
{
	if (state.mode == DictMode::words)
	{
		RegexProbe out;
		out.ok = false;
		out.error = "тестовое поле есть только у словаря РВ";
		return out;
	}
	const std::size_t di = context.find(word);
	const std::size_t at = di == UString::npos ? 0 : di;
	return probe_regex(state.rules, context, at, word);
}

std::string dict_row_text(const DictEditorState& state, std::size_t list_row)
{
	if (list_row >= state.visible.size())
		return {};
	const std::size_t line = state.visible[list_row];
	if (state.mode == DictMode::words)
	{
		if (line >= state.words.words.size())
			return {};
		const DictLine& w = state.words.words[line];
		std::string out = to_utf8(w.yo);
		if (w.ask)
			out += " ?";
		out += "  |  ";
		out += to_utf8(w.ye());
		return out;
	}
	const std::vector<std::size_t> lines = state.rules.rule_lines();
	if (list_row >= lines.size() || lines[list_row] >= state.rules.lines.size())
		return {};
	const RegexLine& r = state.rules.lines[lines[list_row]];
	std::string out = to_utf8(r.text);
	const RuleCheck check = check_regex_rule(r.text);
	if (!check.ok)
		out += "   [ошибка: " + check.error + "]";
	return out;
}

bool dict_row_error(const DictEditorState& state, std::size_t list_row)
{
	if (state.mode != DictMode::rules)
		return false;
	if (list_row >= state.visible.size())
		return false;
	const std::size_t rule_no = state.visible[list_row];
	const std::vector<std::size_t> lines = state.rules.rule_lines();
	if (rule_no >= lines.size() || lines[rule_no] >= state.rules.lines.size())
		return false;
	return !check_regex_rule(state.rules.lines[lines[rule_no]].text).ok;
}

} // namespace yo
