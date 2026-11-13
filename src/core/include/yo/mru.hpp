// Список последних файлов (MRU) в формате yo.ini оригинала.
//
// Строка `[Files] NameN` устроена так: `путь,позиция,флаги` (FORMATS.md §3).
// Позиция — смещение курсора (`SelStart`), флаги — буквы без разделителей:
// `u` — UTF-8, `U` — UTF-16LE, `S` — BOM, `R` — RTF. Порядок списка — FIFO:
// повторное открытие уже записанного файла его не двигает (`AddFileToList`
// оригинала выходит, если файл уже в списке).
//
// Логика живёт в ядре: разбор строки, сдвиг списка и поиск пути без Windows —
// поэтому проверяются тестами.
#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace yo {

struct MruEntry
{
	std::string path;		  // путь файла (UTF-8, как он записан в ini)
	std::size_t position = 0; // позиция курсора, байтовое смещение
	std::string flags;		  // «u», «uS», «R» и т. п.

	[[nodiscard]] bool utf8() const noexcept { return flags.find('u') != std::string::npos; }
	[[nodiscard]] bool utf16() const noexcept { return flags.find('U') != std::string::npos; }
	[[nodiscard]] bool bom() const noexcept { return flags.find('S') != std::string::npos; }
	[[nodiscard]] bool rtf() const noexcept { return flags.find('R') != std::string::npos; }
};

// Максимум файлов в списке (`MaxFileNamesInList` оригинала).
inline constexpr std::size_t kMruMax = 9;

// Разбор значения `Files.NameN`. Мусор не мешает запуску: некорректная
// позиция читается как 0, отсутствие запятых — путь целиком.
[[nodiscard]] MruEntry mru_parse(std::string_view value);
// Обратная запись: `путь,позиция` и, если флаги непусты, `,флаги`.
[[nodiscard]] std::string mru_format(const MruEntry& entry);

// Сравнение путей без учёта регистра (в оригинале — `LC`), с учётом '/'.
[[nodiscard]] bool mru_same_path(std::string_view a, std::string_view b);

class MruList
{
public:
	// Добавить файл в конец списка (FIFO). Если он уже есть — порядок не
	// меняется, обновляются только позиция и флаги. Возвращает индекс.
	std::size_t add(MruEntry entry);
	// Запомнить позицию курсора и кодировку для уже записанного файла.
	void remember(std::string_view path, std::size_t position, std::string flags);
	// Убрать файл (например, если его больше нет на диске).
	void remove(std::string_view path);
	void clear();
	void set(const std::vector<MruEntry>& entries); // из ini, не больше kMruMax

	[[nodiscard]] std::size_t size() const noexcept { return entries_.size(); }
	[[nodiscard]] bool empty() const noexcept { return entries_.empty(); }
	[[nodiscard]] const std::vector<MruEntry>& entries() const noexcept { return entries_; }
	// Индекс файла в списке или kMruMax, если его нет.
	[[nodiscard]] std::size_t find(std::string_view path) const;
	[[nodiscard]] const MruEntry* at(std::size_t index) const;

private:
	std::vector<MruEntry> entries_;
};

} // namespace yo
