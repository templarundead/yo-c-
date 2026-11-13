// Настройки в формате INI (как yo.ini оригинального ёфикатора).
//
// Разбор и запись живут в ядре: формат не зависит от Windows, поэтому
// проверяется тестами на любой платформе, а GUI только читает/пишет файл.
#pragma once

#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace yo {

class Ini
{
public:
	// Разбор текста: строки «[секция]», «ключ=значение», комментарии «;»/«#».
	// Мусорные строки пропускаются — файл настроек не должен мешать запуску.
	void parse(std::string_view text);
	// Сериализация в том же порядке, в каком задавались ключи.
	[[nodiscard]] std::string dump() const;

	[[nodiscard]] std::string get(std::string_view section, std::string_view key,
								  std::string_view fallback = {}) const;
	[[nodiscard]] long long get_int(std::string_view section, std::string_view key,
									long long fallback) const;
	[[nodiscard]] float get_float(std::string_view section, std::string_view key,
								  float fallback) const;
	[[nodiscard]] bool get_bool(std::string_view section, std::string_view key,
								bool fallback) const;

	void set(std::string_view section, std::string_view key, std::string value);
	void set_int(std::string_view section, std::string_view key, long long value);
	void set_float(std::string_view section, std::string_view key, float value);
	void set_bool(std::string_view section, std::string_view key, bool value);

	[[nodiscard]] bool has(std::string_view section, std::string_view key) const;

private:
	struct Entry
	{
		std::string section;
		std::string key;
		std::string value;
	};
	std::vector<Entry> entries_;
};

// Строка «ключ=значение» в UTF-8 без переводов строк (значения с '\n'
// в INI недопустимы).
[[nodiscard]] std::string_view ini_trim(std::string_view text);

} // namespace yo
