#include "yo/yofy.hpp"

#include <algorithm>
#include <array>
#include "yo/cp1251.hpp"

#include <string>

namespace yo {

namespace {

// NoCR: #13 -> "\n" (литерал), #10 отбрасывается.
UString no_cr(UStringView s)
{
	UString out;
	out.reserve(s.size());
	for (char32_t c : s)
	{
		if (c == U'\r')
			out += U"\\n";
		else if (c != U'\n')
			out.push_back(c);
	}
	return out;
}

// Разметка окна контекста: от слова назад и вперёд по half «слов».
// crossed_left/crossed_right — обход упёрся в границу буфера,
// то есть за ним есть текст, который надо учитывать (медленный путь).
struct WindowScan
{
	std::size_t start = 0;
	std::size_t end = 0;
	bool crossed_left = false;
	bool crossed_right = false;
};

WindowScan scan_window(UStringView s, std::size_t word_start, std::size_t word_len, int half)
{
	WindowScan w;
	const std::size_t word_end = word_start + word_len;

	std::size_t start = word_start;
	bool cut_left = false;
	for (int n = 1; n <= half; ++n)
	{
		bool got = false;
		while (start > 0)
		{
			--start;
			if (cyr::is_alpha(s[start]))
			{
				got = true;
				break;
			}
		}
		if (!got)
			w.crossed_left = true;
		got = false;
		while (start > 0)
		{
			--start;
			if (!cyr::is_alpha(s[start]))
			{
				cut_left = (n == half);
				got = true;
				break;
			}
		}
		if (!got)
			w.crossed_left = true;
	}
	if (cut_left && start < s.size())
		++start;

	std::size_t end = std::min(word_end, s.size());
	bool cut_right = false;
	for (int n = 1; n <= half; ++n)
	{
		bool got = false;
		while (end < s.size())
		{
			const char32_t c = s[end++];
			if (cyr::is_alpha(c))
			{
				got = true;
				break;
			}
		}
		if (!got)
			w.crossed_right = true;
		got = false;
		while (end < s.size())
		{
			const char32_t c = s[end++];
			if (!cyr::is_alpha(c))
			{
				cut_right = (n == half);
				got = true;
				break;
			}
		}
		if (!got)
			w.crossed_right = true;
	}
	if (cut_right && end > start)
		--end;

	w.start = start;
	w.end = end;
	return w;
}

// Кавычки в логе не экранируются оригиналом; сохраняем это поведение.
std::string log_text(UStringView s)
{
	return cp1251::encode(s);
}

} // namespace

Yoficator::Yoficator(Dictionary& dict, RegexDictionary* regex_dic, Options opts)
	: dict_(dict), regex_dic_(regex_dic), opts_(opts)
{
}

bool Yoficator::process_line(UString& text, std::size_t line_no, const LineSource& source,
							 std::vector<Change>* changes)
{
	current_line_ = &text;
	changes_ = changes;
	line_no_ = line_no;
	current_source_ = &source;

	std::size_t limit = text.size();
	if (opts_.fb_format)
	{
		// FBFormat: обрезать строку по тэгу <BINARY (без учёта регистра).
		static constexpr UStringView kTag = U"<BINARY";
		UString upper = text;
		for (auto& c : upper)
			c = cyr::to_upper(c);
		if (auto p = upper.find(kTag); p != UString::npos)
			limit = p;
	}

	// Ёфикация начинается с начала строки, поэтому слово не может быть
	// «оборвано» слева (в GUI-варианте появится параметр start_pos).
	bool broken_word = false;
	bool last_alpha = false;
	UString word;
	for (std::size_t j = 0; j < limit; ++j)
	{
		const char32_t c = text[j];
		if (cyr::is_alpha(c))
		{
			word.push_back(c);
			last_alpha = true;
			continue;
		}
		if (last_alpha)
		{
			const std::size_t start = j - word.size();
			const bool cancel = broken_word ? false : check_word_ex(word, start, j, c, line_no, source);
			broken_word = false;
			word.clear();
			if (cancel)
				return false;
		}
		last_alpha = false;
	}
	if (!word.empty())
	{
		const std::size_t start = limit - word.size();
		const char32_t c = limit > 0 ? text[limit - 1] : char32_t{};
		if (!broken_word && check_word_ex(word, start, limit, c, line_no, source))
			return false;
	}
	return true;
}

bool Yoficator::check_word_ex(const UString& word, std::size_t word_start, std::size_t term_index,
							  char32_t current, std::size_t line_no, const LineSource& source)
{
	if (!cyr::is_ye_yo_if(word, opts_.check_yo))
		return false;
	to_check_abbreviation_ =
		opts_.to_confirm_abbr && is_abbreviation(current, line_no, term_index, source);
	to_check_ellipsis_ =
		opts_.to_confirm_ellipsis && is_ellipsis(current, line_no, term_index, source);
	bool to_cancel = false;
	const bool ok = check_word(word, word_start, to_cancel);
	(void)ok;
	return to_cancel;
}

bool Yoficator::check_word(const UString& word, std::size_t offset, bool& to_cancel)
{
	to_cancel = false;
	if (!(word.size() > 1 && cyr::is_ye_yo_in(word)))
		return true;
	if (cyr::is_yo_in(word) && !opts_.check_yo)
		return true;

	current_pos_ = offset;
	int dic_index = -1;
	int focus_n = 0;
	bool wrong_yo = false;
	const UString new_word = select_word(word, offset, to_cancel, dic_index, focus_n, wrong_yo);
	if (to_cancel)
		return true;

	++stats_.checked.a;
	if (new_word.empty())
		++stats_.checked.not_changed;
	else
		++stats_.checked.changed;
	if (was_ask_)
	{
		++stats_.asked.a;
		if (new_word.empty())
			++stats_.asked.not_changed;
		else
			++stats_.asked.changed;
	}
	if (wrong_yo)
		++stats_.wrong_yo;
	add_to_stat(word, new_word, dic_index);
	if (!new_word.empty() && current_line_ != nullptr)
		change_letter(*current_line_, offset, word, new_word, was_ask_, changes_);
	return true;
}

UStringView Yoficator::find_replacement(UStringView word_ye, bool& to_ask, int& index, int& focus_n,
										bool& always) const
{
	to_ask = false;
	index = dict_.find(word_ye);
	if (index < 0)
	{
		focus_n = 1;
		return {};
	}
	const UStringView result = dict_.yo_view(static_cast<std::size_t>(index));
	always = dict_.user_sel(static_cast<std::size_t>(index)) != 0;
	if (!always)
	{
		to_ask = dict_.ask(static_cast<std::size_t>(index)) != 0;
		focus_n = to_ask ? dict_.words_answer(static_cast<std::size_t>(index)) : 2;
	}
	else
	{
		to_ask = false;
		focus_n = dict_.user_sel(static_cast<std::size_t>(index));
	}
	return result;
}

UString Yoficator::select_word(const UString& cword, [[maybe_unused]] std::size_t offset,
							   bool& to_cancel,
							   int& dic_index, int& focus_n, bool& wrong_yo)
{
	to_make_undo_ = false;
	was_ask_ = false;
	focus_n = 0;
	to_cancel = false;

	// ToYe нужен только когда в слове есть «ё»; иначе это та же строка.
	UString word1_buf;
	UStringView word1 = cword;
	if (cyr::is_yo_in(cword))
	{
		word1_buf = cyr::to_ye(cword);
		word1 = word1_buf;
	}
	bool to_ask = false;
	bool always = false;
	UStringView word2 = find_replacement(word1, to_ask, dic_index, focus_n, always);
	// Копия нужна только там, где меняется регистр (или слово берётся из cword
	// с другими буквами); в остальных случаях работаем срезом.
	UString word2_buf;
	const bool cword_has_upper = cyr::is_any_upper(cword);

	to_ask = to_ask || (!word2.empty() && to_check_abbreviation_) ||
			 (!word2.empty() && to_check_ellipsis_) ||
			 (!word2.empty() && opts_.to_confirm_cap && cyr::is_upper(cword.front()) && !always);

	const bool is_yo_word = cyr::is_yo_in(cword);
	wrong_yo = is_yo_word && !cyr::equals_ignore_case(word2, cword);
	const bool replaced_yo = wrong_yo && !word2.empty();
	const bool wrong_ye = !is_yo_word && cyr::equals_ignore_case(word1, cword) &&
						  !to_ask && (focus_n == 2);

	if (replaced_yo)
	{
		word1_buf = cyr::copy_case(word2, cword);
		word1 = word1_buf;
	}
	if ((word2.empty() && is_yo_word) || replaced_yo)
		word2 = cword;
	if (cword_has_upper)
	{
		word2_buf = cyr::copy_case(word2, cword);
		word2 = word2_buf;
	}

	if (!to_ask && opts_.var_only && !wrong_yo)
		return {};

	if (opts_.regexs && to_ask && regex_dic_ != nullptr)
	{
		int word_n = 0;
		if (look_in_regexps(cword, word1, word2, word_n))
		{
			to_ask = false;
			focus_n = word_n;
			++stats_.regex_resolved.a;
			if (((word_n == 1) && (cword == word2)) || ((word_n == 2) && (cword == word1)))
				++stats_.regex_resolved.changed;
			else
				++stats_.regex_resolved.not_changed;
		}
		else
			++stats_.no_regex;
	}

	if (to_ask && opts_.no_var_only)
		return {};

	if (wrong_yo)
	{
		if (opts_.no_var_only)
			return {};
		to_ask = true;
	}

	if (!to_ask && opts_.always_ask && !word2.empty())
		to_ask = true;

	bool skip_word = false;
	if (to_ask)
	{
		AskRequest req;
		req.word1 = word1;
		req.word2 = word2;
		req.wrong_yo = wrong_yo;
		req.replaced_yo = replaced_yo;
		req.wrong_ye = wrong_ye;
		req.focus_first =
			(focus_n != 0 && opts_.propose_last && !wrong_yo) ? (focus_n == 1)
															  : (!cyr::is_yo_in(cword) || wrong_yo);
		req.propose_last = opts_.propose_last;

		++stats_.ask_prompts;
		AskAnswer answer = ask_ ? ask_(req) : AskAnswer::cancel;
		if (answer == AskAnswer::ye_all || answer == AskAnswer::yo_all)
		{
			if (dic_index >= 0)
				dict_.set_user_sel(static_cast<std::size_t>(dic_index),
								   answer == AskAnswer::ye_all ? 1 : 2);
			answer = answer == AskAnswer::ye_all ? AskAnswer::ye : AskAnswer::yo;
		}
		was_ask_ = true;
		to_make_undo_ = answer == AskAnswer::undo;
		if (to_make_undo_)
			answer = AskAnswer::cancel;
		switch (answer)
		{
		case AskAnswer::cancel:
		case AskAnswer::undo:
			focus_n = -1;
			break;
		case AskAnswer::ye:
			focus_n = 1;
			break;
		case AskAnswer::yo:
			focus_n = 2;
			break;
		case AskAnswer::skip:
			focus_n = 0;
			skip_word = true;
			++stats_.ambiguous_skipped;
			break;
		case AskAnswer::ye_all:
		case AskAnswer::yo_all:
			break;
		}
		if (dic_index >= 0 && focus_n > 0)
			dict_.set_words_answer(static_cast<std::size_t>(dic_index), focus_n);
	}

	UString result;
	if (skip_word)
	{
		to_cancel = false;
		return {};
	}
	switch (focus_n)
	{
	case 1:
		result = word1;
		break;
	case 2:
		result = word2;
		break;
	default:
		to_cancel = true;
		return {};
	}
	if (result == cword)
		result.clear();
	return result;
}

void Yoficator::add_to_stat(const UString& old_word, const UString& new_word, int dic_index)
{
	if (dic_index < 0)
		return;
	const std::size_t i = static_cast<std::size_t>(dic_index);
	// Сравнение в CP1251 с заранее посчитанными ключами словаря — без аллокаций.
	cp1251::word_key_lower_into(stat_old_, old_word);
	if (!new_word.empty())
		cp1251::word_key_lower_into(stat_new_, new_word);
	const std::string_view ls_ye = dict_.ye_lc(i);
	const std::string_view ls_yo = dict_.yo_lc(i);
	const std::string_view lnew = new_word.empty() ? std::string_view(stat_old_) : stat_new_;
	const std::string_view lold = stat_old_;
	dict_.note_outcome(i, lold == ls_ye, lold == ls_yo, lnew == ls_ye, lnew == ls_yo);
}

void Yoficator::change_letter(UString& text, std::size_t offset, UStringView old_word,
							  UStringView new_word, bool was_ask, std::vector<Change>* changes)
{
	std::size_t k0 = offset;
	const std::size_t n = std::min(old_word.size(), new_word.size());
	for (std::size_t k = 0; k < n; ++k)
	{
		if (old_word[k] != new_word[k])
		{
			if (changes != nullptr)
				changes->push_back({ k0, old_word[k], new_word[k], was_ask });
			if (k0 < text.size())
				text[k0] = new_word[k];
		}
		++k0;
	}
}

bool Yoficator::is_abbreviation(char32_t c, std::size_t line_no, std::size_t pos,
								const LineSource& source) const
{
	if (c != U'.')
		return false;
	std::size_t ln = line_no;
	const UString* s = source.line(ln);
	if (s == nullptr)
		return false;
	std::size_t p = pos + 1;
	for (;;)
	{
		if (p >= s->size())
		{
			bool found = false;
			for (;;)
			{
				const UString* next = source.line(ln + 1);
				if (next == nullptr)
					break;
				++ln;
				if (!next->empty())
				{
					s = next;
					found = true;
					break;
				}
			}
			if (!found)
				return false;
			p = 0;
			if (p >= s->size())
				return false;
		}
		const char32_t ch = (*s)[p];
		if (cyr::is_lower(ch))
			return true;
		if (!cyr::is_blank(ch))
			return false;
		++p;
	}
}

bool Yoficator::is_ellipsis(char32_t c, std::size_t line_no, std::size_t pos,
							const LineSource& source) const
{
	(void)c;
	const UString* s = source.line(line_no);
	if (s == nullptr || pos >= s->size())
		return false;
	const UStringView tail = UStringView(*s).substr(pos, 3);
	return tail.starts_with(U"...") || tail.starts_with(U"…");
}

Yoficator::WordContext Yoficator::get_context(std::size_t line_no, std::size_t pos,
											  std::size_t word_len, const LineSource& source) const
{
	const int half = std::max(1, opts_.words_buffer_length / 2);

	// Быстрый путь: окно целиком внутри текущей строки — без склейки и копий.
	if (const UString* cur = source.line(line_no))
	{
		const std::size_t p = std::min(pos, cur->size());
		const WindowScan w = scan_window(*cur, p, word_len, half);
		if (!w.crossed_left && !w.crossed_right)
			return { UStringView(*cur).substr(w.start, w.end > w.start ? w.end - w.start : 0),
					 p - w.start };
	}

	// Окно из соседних строк, склеенное через CRLF (как PrevChar/NextChar).
	const std::size_t back = 2;
	const std::size_t fwd = 2;
	const std::size_t first_line = line_no > back ? line_no - back : 0;

	ctx_joined_.clear();
	std::size_t word_start_off = 0;
	std::size_t ln = first_line;
	for (; ln <= line_no + fwd; ++ln)
	{
		const UString* s = source.line(ln);
		if (s == nullptr)
		{
			if (ln <= line_no)
				continue; // строка вне доступного окна — пропускаем
			break;
		}
		if (ln != first_line)
			ctx_joined_ += U"\r\n";
		if (ln == line_no)
			word_start_off = ctx_joined_.size() + std::min(pos, s->size());
		ctx_joined_.append(*s);
	}

	const WindowScan w = scan_window(ctx_joined_, word_start_off, word_len, half);
	return { UStringView(ctx_joined_).substr(w.start, w.end > w.start ? w.end - w.start : 0),
			 word_start_off - w.start };
}

bool Yoficator::look_in_regexps(const UString& cword, UStringView word1, UStringView word2,
								int& word_n)
{
	if (regex_dic_ == nullptr || regex_dic_->empty() || current_source_ == nullptr)
	{
		++stats_.no_regex;
		return false;
	}
	const WordContext wc = get_context(line_no_, current_pos_, cword.size(), *current_source_);
	word_n = 0;
	std::size_t rule_index = 0;
	if (!regex_dic_->lookup(wc.context, wc.di, word1, word2, word_n, rule_index))
	{
		++stats_.no_regex;
		return false;
	}
	regex_dic_->note_used(rule_index);

	if (regex_log_)
	{
		const UStringView word_new = (word_n == 1) ? word1 : word2;
		UString words = cword;
		if (!(cword == word_new))
		{
			words += U"/";
			words += word_new;
		}
		UString marked(wc.context.substr(0, wc.di));
		marked += U"<";
		marked += words;
		marked += U">";
		marked += wc.context.substr(wc.di + cword.size());
		// Формат оригинала: LineN(0-based):PosN(1-based); "контекст"; "правило".
		std::string line = std::to_string(line_no_) + ":" + std::to_string(current_pos_ + 1) +
						   "; \"" + log_text(no_cr(marked)) + "\"; \"" +
						   log_text(regex_dic_->rules()[rule_index].valid) + "\"";
		regex_log_(line);
	}
	return true;
}

} // namespace yo
