// Запомненные ответы «Всегда Е/Ё» (UserSel словаря) — файл рядом с yo.ini.
//
// В оригинале выбор «Всегда Е/Ё» жил только до перезапуска. Здесь он
// сохраняется отдельным файлом (словарь при этом не правится: yox.dat
// остаётся в формате поставки, FORMATS.md §1).
//
// Формат — текст UTF-8, по строке на слово:
//   ; комментарий
//   <слово в форме «е»>\t<1 — всегда Е | 2 — всегда Ё>
// Ключ — именно слово, а не индекс в словаре: файл переживает обновление
// словаря, а несовпавшие строки при загрузке просто игнорируются.
#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace yo {

struct UserSelEntry
{
	std::string word;	 // форма с «е» (UTF-8), как ключ поиска по словарю
	int value = 0;		 // 1 — всегда «Е», 2 — всегда «Ё»
};

// Разбор файла: неизвестные значения, пустые слова и мусор пропускаются.
[[nodiscard]] std::vector<UserSelEntry> parse_user_selections(std::string_view text);

// Запись (без заголовка-комментария — его добавляет вызывающий).
[[nodiscard]] std::string format_user_selections(const std::vector<UserSelEntry>& entries);

// Ограничение на размер файла: больше — уже не «запомненные ответы»,
// а что-то не то.
inline constexpr std::size_t kUserSelMax = 200000;

} // namespace yo
