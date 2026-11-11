// UTF-8 <-> UTF-16 и нормализация переводов строк.
//
// Ядру нужен UTF-8 (буфер, словари), Win32 — UTF-16 (DirectWrite, буфер
// обмена). Конвертация сделана здесь, без Win32-заголовков, чтобы её можно
// было тестировать на любой платформе. Неверные последовательности UTF-8
// заменяются на U+FFFD, суррогаты вне пар — тоже.
#pragma once

#include "yo/textbuf.hpp"

#include <cstddef>
#include <string>
#include <string_view>

namespace yo {

[[nodiscard]] std::u16string utf8_to_utf16(std::string_view utf8);
[[nodiscard]] std::string utf16_to_utf8(std::u16string_view utf16);

// Длина UTF-8 символа, начинающегося с байта c (1..4), 0 — байт-продолжение.
[[nodiscard]] std::size_t utf8_char_len(char c) noexcept;

// Есть ли в тексте неверные последовательности UTF-8.
[[nodiscard]] bool utf8_valid(std::string_view utf8) noexcept;

// Байты EOL для стиля (пусто для Eol::none).
[[nodiscard]] std::string_view eol_bytes(Eol eol) noexcept;

// Перевести все CRLF/LF/CR на заданный стиль (буфер обмена, вставка).
[[nodiscard]] std::string normalize_newlines(std::string_view text, Eol eol);

#ifdef _WIN32
// wchar_t в Windows — 16 бит: те же функции без потери данных.
[[nodiscard]] std::wstring utf8_to_wide(std::string_view utf8);
[[nodiscard]] std::string wide_to_utf8(std::wstring_view wide);
#endif

} // namespace yo
