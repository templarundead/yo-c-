// Редактор словарей (этап 5 ARCHITECTURE.md §5.3): yox.dat и yo_regexpr.dat.
//
// Форматы файлов не меняются (FORMATS.md §1, §2): CP1251, CRLF, шапка из
// '#'-комментариев сохраняется как есть, слово — форма с «ё» и суффикс «?»,
// правила РВ — строки-шаблоны. Вся логика правки живёт здесь, GUI только
// рисует состояние и шлёт клавиши/клики — поэтому поведение проверяется
// тестами без Windows.
#pragma once

#include "yo/cyr.hpp"
#include "yo/dict.hpp"
#include "yo/regexdict.hpp"

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace yo {

// ---------------------------------------------------------------- словарь

// Строка словаря: форма с ё/Ё (как в файле) и признак «?» (спрашивать).
struct DictLine
{
	UString yo;
	bool ask = false;

	[[nodiscard]] UString ye() const { return cyr::to_ye(yo); }
};

// Почему слово не годится для словаря.
enum class WordProblem
{
	none,
	empty,	 // пустая строка
	bad_chars, // не только кириллица (IsWord оригинала)
	no_yo,	 // нет «ё»/«Ё» (в yox.dat только формы с ё)
};

struct WordCheck
{
	WordProblem problem = WordProblem::none;
	std::string message; // по-русски, для строки статуса редактора
	[[nodiscard]] bool ok() const noexcept { return problem == WordProblem::none; }
};

// Проверка словоформы (без учёта «?» — его ставит/снимает флаг ask).
[[nodiscard]] WordCheck check_dict_word(UStringView yo);

// Файл словаря целиком.
struct DictFile
{
	std::string version;		   // текст после «Версия:» (Trim)
	std::vector<UString> header;   // строки шапки как есть (вместе с '#')
	std::vector<DictLine> words;   // в порядке файла

	// Разбор CP1251-текста: нарушения порядка/формы не мешают разбору —
	// их показывает check_dict_order/проверка каждой строки.
	[[nodiscard]] static DictFile parse(std::string_view cp1251_text);
	// Обратная запись: шапка, затем слова построчно, CP1251 + CRLF.
	[[nodiscard]] std::string format() const;
	[[nodiscard]] std::size_t size() const noexcept { return words.size(); }
};

// Нарушение порядка CmpYeEx: индекс первого «не по порядку» и его слово.
struct DictOrder
{
	bool sorted = true;
	std::size_t index = 0;
	std::string word; // в UTF-8, для сообщения
};

[[nodiscard]] DictOrder check_dict_order(const DictFile& file);

// Результат правки списка слов: индекс затронутой строки и сообщение.
struct DictEdit
{
	bool ok = false;
	std::size_t index = 0;
	std::string message;
};

// Вставка с сохранением порядка (CmpYeEx); дубликат по форме с «е» — отказ.
[[nodiscard]] DictEdit dict_insert(DictFile& file, UStringView yo, bool ask);
// Замена строки index: переставляется на своё место (порядок сохраняется).
[[nodiscard]] DictEdit dict_replace(DictFile& file, std::size_t index, UStringView yo, bool ask);
// Удаление строки; false — индекса нет.
bool dict_erase(DictFile& file, std::size_t index);
// Поиск по форме с «е» (CmpYeEx, без учёта регистра); npos — нет такого.
[[nodiscard]] std::size_t dict_find_word(const DictFile& file, UStringView word);
// Сортировка по CmpYeEx (после ручных правок/загрузки «грязного» файла).
void dict_sort(DictFile& file);

// Фильтр таблицы: подстрока без учёта регистра и е/ё; only_ask — только «?».
struct DictFilter
{
	UString query;
	bool only_ask = false;
};

[[nodiscard]] std::vector<std::size_t> dict_filter(const DictFile& file, const DictFilter& filter);

// ---------------------------------------------------------------- правила РВ

// Строка файла правил: текст как есть и признак «это правило, не комментарий».
struct RegexLine
{
	UString text;
	bool rule = false;
};

struct RegexFile
{
	std::string version;
	std::vector<RegexLine> lines;

	[[nodiscard]] static RegexFile parse(std::string_view cp1251_text);
	[[nodiscard]] std::string format() const;
	// Индексы строк-правил в порядке файла.
	[[nodiscard]] std::vector<std::size_t> rule_lines() const;
	[[nodiscard]] std::size_t rule_count() const;
};

// Проверка одного правила: трансляция (MakeValidRegExpr + RE2) и компиляция.
struct RuleCheck
{
	bool ok = false;
	std::string error;	 // текст ошибки RE2 (пусто, если ok)
	std::string valid;	 // UTF-8 после MakeValidRegExpr
	std::string pattern; // итоговый шаблон RE2 (UTF-8)
};

[[nodiscard]] RuleCheck check_regex_rule(UStringView raw);

// Тестовое поле: контекст + целевое слово → ответ словаря РВ.
struct RegexProbe
{
	bool ok = true;		 // false — в словаре есть некомпилируемое правило
	bool matched = false;
	std::size_t line = 0; // индекс строки правила в файле
	std::size_t rule_no = 0;
	int word_n = 0;		 // 1 — подошла форма с «е», 2 — с «ё»
	std::string error;
	std::string word_ye;   // UTF-8: проверяемая форма с «е»
	std::string word_yo;   // UTF-8: форма с «ё»
};

// Первое сработавшее правило на контексте (как LookInRegExps).
// word — слово в контексте (di — его позиция в кодпоинтах); форма с «ё»
// получается заменой первого «е» на «ё», если «ё» в слове нет.
[[nodiscard]] RegexProbe probe_regex(const RegexFile& file, UStringView context, std::size_t di,
									 UStringView word);

// ---------------------------------------------------------------- состояние

enum class DictMode
{
	words, // основной словарь
	rules, // словарь регулярных выражений
};

// Порядок обхода полей ввода клавишей Tab.
enum class DictField
{
	filter, // поиск по таблице
	text,	// словоформа или правило
};

struct DictEditorState
{
	DictMode mode = DictMode::words;
	DictFile words;
	RegexFile rules;
	DictFilter filter;
	std::vector<std::size_t> visible; // индексы строк под фильтром
	std::size_t cursor = 0;			  // строка списка (индекс в visible)
	std::size_t top = 0;			  // первая видимая строка списка
	UString edit;					  // поле «словоформа/правило»
	bool edit_ask = false;			  // «?» для слова
	DictField field = DictField::filter;
	std::string message;
	bool message_error = false;
	bool dirty = false;
	std::string path_words; // откуда читали (для сохранения)
	std::string path_rules;

	// Текущая строка файла (npos — список пуст).
	[[nodiscard]] std::size_t selected_line() const;
};

// Загрузка обоих словарей; отсутствие файла — не ошибка (пустой словарь).
void dict_editor_load(DictEditorState& state, std::string path_words, std::string path_rules);
// Пересобрать visible по фильтру, подрезать cursor/top.
void dict_editor_refilter(DictEditorState& state);
// Движение по списку (delta может быть отрицательным), PgUp/PgDn — rows.
void dict_editor_move(DictEditorState& state, long delta);
// Взять текущую строку в поле ввода.
void dict_editor_pick(DictEditorState& state);
// Заменить выбранную строку содержимым поля ввода (слово или правило).
[[nodiscard]] bool dict_editor_apply(DictEditorState& state);
// Добавить новое слово/правило из поля ввода (выбор строки не важен).
[[nodiscard]] bool dict_editor_add(DictEditorState& state);
// Удалить текущую строку.
[[nodiscard]] bool dict_editor_erase(DictEditorState& state);
// Сохранить оба словаря (атомарно, через .yo-tmp + rename).
[[nodiscard]] bool dict_editor_save(DictEditorState& state);
// Запустить тестовое поле (только режим правил).
[[nodiscard]] RegexProbe dict_editor_probe(const DictEditorState& state, UStringView context,
										   UStringView word);

// Подпись строки таблицы: «словоформа | ? | форма с е» (только текст).
[[nodiscard]] std::string dict_row_text(const DictEditorState& state, std::size_t list_row);
// У правила в строке ошибка компиляции (в таблице рисуется красным).
[[nodiscard]] bool dict_row_error(const DictEditorState& state, std::size_t list_row);

} // namespace yo
