// Кириллические операции ядра.
//
// Семантика повторяет функции оригинала (Main.pas):
//   IsLower/IsUpper/IsAlpha — только кириллица (а-я, ё, А-Я, Ё);
//   LC — AnsiLowerCase для CP1251: ASCII A-Z -> a-z, А-Я -> а-я, Ё -> ё;
//   ToYe — ё -> е, Ё -> Е;
//   CopyCase — регистр по образцу (по позициям).
#pragma once

#include <algorithm>
#include <string>
#include <string_view>

namespace yo {

using UString = std::u32string;
using UStringView = std::u32string_view;

namespace cyr {

inline constexpr char32_t kYeLower = U'е';
inline constexpr char32_t kYeUpper = U'Е';
inline constexpr char32_t kYoLower = U'ё';
inline constexpr char32_t kYoUpper = U'Ё';

// IsLower оригинала: (c >= 'а') and (c <= 'я') or (c = 'ё').
[[nodiscard]] constexpr bool is_lower(char32_t c) noexcept
{
	return (c >= U'а' && c <= U'я') || c == kYoLower;
}

// IsUpper оригинала: (c >= 'А') and (c <= 'Я') or (c = 'Ё').
[[nodiscard]] constexpr bool is_upper(char32_t c) noexcept
{
	return (c >= U'А' && c <= U'Я') || c == kYoUpper;
}

// IsAlpha оригинала: буква (кириллица) — латиница буквой НЕ считается.
[[nodiscard]] constexpr bool is_alpha(char32_t c) noexcept
{
	return is_lower(c) || is_upper(c);
}

[[nodiscard]] constexpr bool is_ascii_digit(char32_t c) noexcept
{
	return c >= U'0' && c <= U'9';
}

[[nodiscard]] constexpr bool is_ascii_alpha(char32_t c) noexcept
{
	return (c >= U'a' && c <= U'z') || (c >= U'A' && c <= U'Z');
}

// MyWordChars оригинала: 0-9, a-z, A-Z, А-Я, а-я (с Ё/ё). Для \b и \w.
[[nodiscard]] constexpr bool is_word_char(char32_t c) noexcept
{
	return is_ascii_digit(c) || is_ascii_alpha(c) || is_alpha(c);
}

// MySpaceChars оригинала: ' ', #9, #10, #13, #12, #160.
[[nodiscard]] constexpr bool is_blank(char32_t c) noexcept
{
	return c == U' ' || c == U'\t' || c == U'\n' || c == U'\r' || c == U'\f' || c == U'\u00A0';
}

inline constexpr bool is_ye(char32_t c) noexcept
{
	return c == kYeLower || c == kYeUpper;
}

inline constexpr bool is_yo(char32_t c) noexcept
{
	return c == kYoLower || c == kYoUpper;
}

inline constexpr bool is_ye_yo(char32_t c) noexcept
{
	return is_ye(c) || is_yo(c);
}

// AnsiLowerCase (CP1251) в терминах Unicode.
[[nodiscard]] constexpr char32_t to_lower(char32_t c) noexcept
{
	if (c >= U'A' && c <= U'Z')
		return c + (U'a' - U'A');
	if (c >= U'А' && c <= U'Я')
		return c + (U'а' - U'А');
	if (c == kYoUpper)
		return kYoLower;
	return c;
}

// AnsiUpperCase (CP1251) в терминах Unicode.
[[nodiscard]] constexpr char32_t to_upper(char32_t c) noexcept
{
	if (c >= U'a' && c <= U'z')
		return c - (U'a' - U'A');
	if (c >= U'а' && c <= U'я')
		return c - (U'а' - U'А');
	if (c == kYoLower)
		return kYoUpper;
	return c;
}

[[nodiscard]] inline UString to_lower(UStringView s)
{
	UString r;
	r.reserve(s.size());
	for (char32_t c : s)
		r.push_back(to_lower(c));
	return r;
}

// ToYe: все ё заменяются на е, Ё на Е.
[[nodiscard]] inline UString to_ye(UStringView s)
{
	UString r;
	r.reserve(s.size());
	for (char32_t c : s)
		r.push_back(is_yo(c) ? (c == kYoLower ? kYeLower : kYeUpper) : c);
	return r;
}

[[nodiscard]] inline bool is_yo_in(UStringView s)
{
	return std::ranges::any_of(s, [](char32_t c) { return is_yo(c); });
}

[[nodiscard]] inline bool is_ye_in(UStringView s)
{
	return std::ranges::any_of(s, [](char32_t c) { return is_ye(c); });
}

[[nodiscard]] inline bool is_ye_yo_in(UStringView s)
{
	return std::ranges::any_of(s, [](char32_t c) { return is_ye_yo(c); });
}

// IsYeYoIf: есть «е» или (если check_yo) «ё». Гейт проверки слова.
[[nodiscard]] inline bool is_ye_yo_if(UStringView s, bool check_yo)
{
	for (char32_t c : s)
	{
		if (is_ye(c))
			return true;
		if (check_yo && is_yo(c))
			return true;
	}
	return false;
}

[[nodiscard]] inline bool is_any_upper(UStringView s)
{
	return std::ranges::any_of(s, [](char32_t c) { return is_upper(c); });
}

// Равенство без учёта регистра (ToLowerCase с обеих сторон, без аллокаций).
[[nodiscard]] inline bool equals_ignore_case(UStringView a, UStringView b) noexcept
{
	if (a.size() != b.size())
		return false;
	for (size_t i = 0; i < a.size(); ++i)
		if (to_lower(a[i]) != to_lower(b[i]))
			return false;
	return true;
}

// CopyCase: слово w с заглавными буквами там, где они в w_case.
[[nodiscard]] inline UString copy_case(UStringView w, UStringView w_case)
{
	UString r(w);
	const size_t n = std::min(w.size(), w_case.size());
	for (size_t i = 0; i < n; ++i)
		if (is_upper(w_case[i]))
			r[i] = to_upper(r[i]);
	return r;
}

} // namespace cyr

} // namespace yo
