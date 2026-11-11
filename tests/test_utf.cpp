// UTF-8 <-> UTF-16 и нормализация переводов строк (общие для GUI и CLI).

#include "yo/utf.hpp"

#include <catch2/catch_test_macros.hpp>

#include <string>

using namespace yo;

TEST_CASE("utf8 <-> utf16: кириллица, латиница, эмодзи", "[utf]")
{
	const std::string utf8 = "Привет, мир! e = mc² 🎉";
	const std::u16string u16 = utf8_to_utf16(utf8);
	// 21 кодпоинт BMP + эмодзи суррогатной парой = 23 единицы UTF-16.
	CHECK(u16.size() == 23);
	CHECK(utf16_to_utf8(u16) == utf8);
}

TEST_CASE("utf8: неверные последовательности заменяются на U+FFFD", "[utf]")
{
	CHECK(utf8_valid("ок"));
	CHECK_FALSE(utf8_valid("\xD0"));			  // обрезанный символ
	CHECK_FALSE(utf8_valid("\xD0\x20"));		  // нет байта-продолжения
	CHECK_FALSE(utf8_valid("\xED\xA0\x80"));	  // суррогат в UTF-8
	const std::u16string u16 = utf8_to_utf16("\xD0");
	REQUIRE(u16.size() == 1);
	CHECK(u16[0] == 0xFFFD);
}

TEST_CASE("utf16: одиночный суррогат заменяется", "[utf]")
{
	const std::u16string broken(1, static_cast<char16_t>(0xD83C));
	CHECK(utf16_to_utf8(broken) == "\xEF\xBF\xBD");
}

TEST_CASE("utf8_char_len", "[utf]")
{
	CHECK(utf8_char_len('a') == 1);
	CHECK(utf8_char_len(static_cast<char>(0xD0)) == 2);
	CHECK(utf8_char_len(static_cast<char>(0xE2)) == 3);
	CHECK(utf8_char_len(static_cast<char>(0xF0)) == 4);
	CHECK(utf8_char_len(static_cast<char>(0x80)) == 0);
}

TEST_CASE("eol_bytes и normalize_newlines", "[utf]")
{
	CHECK(eol_bytes(Eol::lf) == "\n");
	CHECK(eol_bytes(Eol::crlf) == "\r\n");
	CHECK(eol_bytes(Eol::cr) == "\r");
	CHECK(eol_bytes(Eol::none).empty());

	CHECK(normalize_newlines("a\r\nb\nc\rd", Eol::lf) == "a\nb\nc\nd");
	CHECK(normalize_newlines("a\r\nb\nc\rd", Eol::crlf) == "a\r\nb\r\nc\r\nd");
	CHECK(normalize_newlines("a\r\nb", Eol::cr) == "a\rb");
	CHECK(normalize_newlines("без переводов", Eol::crlf) == "без переводов");
}
