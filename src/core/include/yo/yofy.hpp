// Движок ёфикации: точный перенос логики оригинала
// (SelectWord / FindReplacement / LookInRegExps / CheckWord / ChangeLetter).
//
// Движок не знает ни про Win32, ни про документ: он обрабатывает одну строку,
// получая доступ к соседним строкам через LineSource (для контекста РВ,
// сокращений и многоточий).
#pragma once

#include "yo/cyr.hpp"
#include "yo/dict.hpp"
#include "yo/regexdict.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace yo {

struct Options
{
	bool always_ask = false;		 // Yo/AlwaysAsk
	bool mark = true;				 // Yo/Mark (влияет только на подсветку GUI)
	bool check_yo = true;			 // Yo/CheckYo
	bool no_var_only = false;		 // Yo/NoVarOnly
	bool var_only = false;			 // Yo/VarOnly
	bool propose_last = true;		 // Yo/ProposeLast
	bool fb_format = false;			 // Yo/FBFormat
	bool regexs = true;				 // Yo/RegExprs
	bool to_confirm_abbr = true;	 // Yo/ToConfirmAbbr
	bool to_confirm_ellipsis = true; // Yo/ToConfirmEllipsis
	bool to_confirm_cap = false;	 // Yo/ToConfirmCap
	int lines_below = 2;			 // Yo/LinesBelow
	int words_buffer_length = 5;	 // Yo/WordsBufferLength (всегда 5 в оригинале)
};

// Доступ к строкам документа (для контекста и проверок вперёд).
class LineSource
{
public:
	virtual ~LineSource() = default;
	[[nodiscard]] virtual std::size_t line_count() const = 0;
	[[nodiscard]] virtual const UString* line(std::size_t index) const = 0;
};

// skip — только для CLI (--on-ambiguous=skip): слово не меняется,
// обработка продолжается (в оригинале такого ответа нет).
enum class AskAnswer : std::uint8_t { ye, yo, ye_all, yo_all, undo, cancel, skip };

struct AskRequest
{
	UString word1;				// вариант без ё
	UString word2;				// вариант с ё (может быть пустым)
	bool wrong_yo = false;		// «вероятно, здесь должна быть Е»
	bool replaced_yo = false;	// «странное расположение Ё»
	bool wrong_ye = false;		// «вероятно, здесь должна быть Ё»
	bool focus_first = false;	// фокус на «Е»
	bool propose_last = false;	// подсказка из словаря (WordToFocusN)
	// Где слово лежит в документе: GUI подсвечивает его в тексте и показывает
	// предложение вокруг (как AskPanel оригинала).
	std::size_t line = 0;		// номер строки
	std::size_t offset = 0;		// начало слова в строке, кодовые точки (UString)
	std::size_t length = 0;		// длина слова, кодовые точки (UString)
};

using AskFn = std::function<AskAnswer(const AskRequest&)>;
using RegexLogFn = std::function<void(std::string_view)>;

struct LetterStat
{
	std::uint64_t a = 0;
	std::uint64_t changed = 0;
	std::uint64_t not_changed = 0;

	void reset() noexcept { a = changed = not_changed = 0; }
};

struct Stats
{
	LetterStat checked;			  // CheckedLS
	LetterStat asked;			  // AskedLS
	LetterStat regex_resolved;	  // RELS
	std::uint64_t no_regex = 0;	  // NoRELS
	std::uint64_t wrong_yo = 0;	  // WrongYoLS
	std::uint64_t ask_prompts = 0;	  // сколько раз спрашивали (для CLI-отчёта)
	std::uint64_t ambiguous_skipped = 0; // неоднозначные, пропущенные политикой CLI

	void reset() noexcept
	{
		checked.reset();
		asked.reset();
		regex_resolved.reset();
		no_regex = 0;
		wrong_yo = 0;
		ask_prompts = 0;
		ambiguous_skipped = 0;
	}
};

// Одна замена буквы (для подсветки/undo в GUI).
struct Change
{
	std::size_t offset = 0; // смещение в строке (кодпоинты)
	char32_t old_ch = 0;
	char32_t new_ch = 0;
	bool was_ask = false;	// интерактивная замена
};

class Yoficator
{
public:
	Yoficator(Dictionary& dict, RegexDictionary* regex_dic, Options opts);

	void set_ask(AskFn fn) { ask_ = std::move(fn); }
	void set_regex_log(RegexLogFn fn) { regex_log_ = std::move(fn); }

	// Обработка одной строки (изменяет её на месте: только равные по длине
	// замены е<->ё). changes — накопленные замены строки.
	// Возвращает false, если обработка прервана (Esc / cancel всех слов).
	bool process_line(UString& text, std::size_t line_no, const LineSource& source,
					  std::vector<Change>* changes);

	// Пользователь выбрал «Откат» на последнем вопросе: вызывающий должен
	// отменить все изменения этого прохода.
	[[nodiscard]] bool undo_requested() const noexcept { return to_make_undo_; }

	[[nodiscard]] const Stats& stats() const noexcept { return stats_; }
	[[nodiscard]] Stats& stats() noexcept { return stats_; }
	[[nodiscard]] const Options& options() const noexcept { return opts_; }
	void set_options(const Options& o) { opts_ = o; }

private:
	// Переиспользуемые буферы статистики (ключи CP1251).
	std::string stat_old_;
	std::string stat_new_;

	struct WordContext
	{
		UStringView context;	// окно контекста (вид на строку или на буфер)
		std::size_t di = 0;		// смещение слова внутри окна
	};

	// Буфер склейки строк для «медленного» пути (окно выходит за строку).
	mutable UString ctx_joined_;

	[[nodiscard]] bool check_word_ex(const UString& word, std::size_t word_start,
									 std::size_t term_index, char32_t current, std::size_t line_no,
									 const LineSource& source);
	[[nodiscard]] bool check_word(const UString& word, std::size_t offset, bool& to_cancel);
	[[nodiscard]] UString select_word(const UString& word, std::size_t offset, bool& to_cancel,
									  int& dic_index, int& focus_n, bool& wrong_yo);
	[[nodiscard]] UStringView find_replacement(UStringView word_ye, bool& to_ask, int& index,
											   int& focus_n, bool& always) const;
	[[nodiscard]] bool look_in_regexps(const UString& word, UStringView word1, UStringView word2,
									   int& word_n);
	[[nodiscard]] WordContext get_context(std::size_t line_no, std::size_t pos,
										  std::size_t word_len, const LineSource& source) const;
	[[nodiscard]] bool is_abbreviation(char32_t c, std::size_t line_no, std::size_t pos,
									   const LineSource& source) const;
	[[nodiscard]] bool is_ellipsis(char32_t c, std::size_t line_no, std::size_t pos,
								   const LineSource& source) const;
	void add_to_stat(const UString& old_word, const UString& new_word, int dic_index);
	void change_letter(UString& text, std::size_t offset, UStringView old_word,
					   UStringView new_word, bool was_ask, std::vector<Change>* changes);

	Dictionary& dict_;
	RegexDictionary* regex_dic_;
	Options opts_;
	AskFn ask_;
	RegexLogFn regex_log_;
	Stats stats_;

	// Состояние текущего слова (аналог глобальных переменных оригинала).
	bool was_ask_ = false;
	bool to_make_undo_ = false;
	bool to_check_abbreviation_ = false;
	bool to_check_ellipsis_ = false;
	UString* current_line_ = nullptr;
	std::vector<Change>* changes_ = nullptr;
	std::size_t line_no_ = 0;
	std::size_t current_pos_ = 0;
	const LineSource* current_source_ = nullptr;
};

} // namespace yo
