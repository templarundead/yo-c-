// Windows-1251 <-> Unicode (UTF-32) и «словарные ключи».
//
// Словарь yox.dat хранится в CP1251 и отсортирован по байтовому порядку
// CP1251 (ё = 0xB8 < е = 0xE5), поэтому ключи слов сравниваются
// именно как байты CP1251 — иначе паритет с оригиналом недостижим.
#pragma once

#include "yo/cyr.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace yo::cp1251 {

[[nodiscard]] char32_t to_unicode(std::uint8_t b) noexcept;

// Байт CP1251 для символа; std::nullopt, если символ не представим.
[[nodiscard]] std::optional<std::uint8_t> from_unicode(char32_t c) noexcept;

[[nodiscard]] UString decode(std::string_view bytes);

// Кодирование; непредставимые символы заменяются на '?'
// (счётчик замен — в out_unmapped, если передан).
[[nodiscard]] std::string encode(UStringView text, std::size_t* out_unmapped = nullptr);

// Ключ словаря: слово (кириллица/ASCII) в байтах CP1251.
// Непредставимые символы становятся '?', чего не бывает для слов,
// отобранных токенизатором (только а-я, ё, А-Я, Ё).
[[nodiscard]] std::string word_key(UStringView word);
void word_key_into(std::string& out, UStringView word);
// То же для Unicode-слова: нижний регистр в CP1251 (для статистики).
void word_key_lower_into(std::string& out, UStringView word);

// Байтовое понижение регистра ключа (аналог AnsiLowerCase для CP1251).
[[nodiscard]] std::string lower_key(std::string_view key);
void lower_key_into(std::string& out, std::string_view key);

} // namespace yo::cp1251
