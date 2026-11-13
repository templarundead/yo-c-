// Поиск и замена по документу: без Win32, поэтому проверяется тестами.
//
// Работает поверх TextBuffer кусками (файл не копируется целиком), умеет
// искать без учёта регистра (кириллица — через cyr::to_lower) и «только
// слово целиком». Замена выполняется вызывающим через TextBuffer.
#pragma once

#include "yo/textbuf.hpp"

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace yo {

struct FindOptions
{
	bool case_sensitive = false; // учитывать регистр
	bool whole_word = false;	 // только слово целиком
};

// Следующее вхождение образца начиная с from (включительно).
// std::string::npos — не найдено.
[[nodiscard]] std::size_t find_next(const TextBuffer& buf, std::size_t from, std::string_view needle,
									const FindOptions& opts = {});
// Предыдущее вхождение строго левее from.
[[nodiscard]] std::size_t find_prev(const TextBuffer& buf, std::size_t from, std::string_view needle,
									const FindOptions& opts = {});

// Все вхождения в диапазон [from, to), но не больше max_hits.
[[nodiscard]] std::vector<std::size_t> find_all(const TextBuffer& buf, std::size_t from, std::size_t to,
												std::string_view needle, const FindOptions& opts = {},
												std::size_t max_hits = 10000);

// Совпадения внутри одной строки (для подсветки видимых строк).
[[nodiscard]] std::vector<std::size_t> find_in_line(std::string_view line, std::string_view needle,
													const FindOptions& opts = {});

// Поиск по образцу-регулярному выражению (синтаксис RE2 — тот же, что у
// словаря РВ, FORMATS.md §2). Ищем построчно, начиная со строки от `from`.
struct FindRegexResult
{
	std::size_t begin = std::string::npos;
	std::size_t end = std::string::npos;
	std::string error; // пусто — выражение разобрано
	[[nodiscard]] bool found() const noexcept { return begin != std::string::npos; }
};

[[nodiscard]] FindRegexResult find_regex(const TextBuffer& buf, std::size_t from,
									std::string_view pattern, bool case_sensitive = false);
[[nodiscard]] FindRegexResult find_regex_prev(const TextBuffer& buf, std::size_t before,
										 std::string_view pattern, bool case_sensitive = false);

// Заменить все вхождения в [from, to) одной группой undo. Возвращает число
// замен. Регистр заменяемого текста сохраняется не побайтово: вставляется
// ровно replacement (как «Заменить всё» в обычном редакторе).
std::size_t replace_all(TextBuffer& buf, std::size_t from, std::size_t to, std::string_view needle,
						std::string_view replacement, const FindOptions& opts = {});

} // namespace yo
