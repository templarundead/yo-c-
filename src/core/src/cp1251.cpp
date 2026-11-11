#include "yo/cp1251.hpp"

#include <array>

namespace yo::cp1251 {

namespace {

constexpr std::array<char32_t, 64> kHigh =
{
	// 0x80
	0x0402, 0x0403, 0x201A, 0x0453, 0x201E, 0x2026, 0x2020, 0x2021,
	0x20AC, 0x2030, 0x0409, 0x2039, 0x040A, 0x040C, 0x040B, 0x040F,
	// 0x90
	0x0452, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014,
	0x0098, 0x2122, 0x0459, 0x203A, 0x045A, 0x045C, 0x045B, 0x045F,
	// 0xA0
	0x00A0, 0x040E, 0x045E, 0x0408, 0x00A4, 0x0490, 0x00A6, 0x00A7,
	0x0401, 0x00A9, 0x0404, 0x00AB, 0x00AC, 0x00AD, 0x00AE, 0x0407,
	// 0xB0
	0x00B0, 0x00B1, 0x0406, 0x0456, 0x0491, 0x00B5, 0x00B6, 0x00B7,
	0x0451, 0x2116, 0x0454, 0x00BB, 0x0458, 0x0405, 0x0455, 0x0457,
};

} // namespace

char32_t to_unicode(std::uint8_t b) noexcept
{
	if (b < 0x80)
		return b;
	if (b < 0xC0)
		return kHigh[b - 0x80];
	// 0xC0..0xFF -> U+0410..U+044F (А..я), без Ё.
	return 0x0410 + (b - 0xC0);
}

std::optional<std::uint8_t> from_unicode(char32_t c) noexcept
{
	if (c < 0x80)
		return static_cast<std::uint8_t>(c);
	if (c >= 0x0410 && c <= 0x044F)
		return static_cast<std::uint8_t>(0xC0 + (c - 0x0410));
	for (std::size_t i = 0; i < kHigh.size(); ++i)
		if (kHigh[i] == c)
			return static_cast<std::uint8_t>(0x80 + i);
	return std::nullopt;
}

UString decode(std::string_view bytes)
{
	UString out;
	out.reserve(bytes.size());
	for (char ch : bytes)
		out.push_back(to_unicode(static_cast<std::uint8_t>(static_cast<unsigned char>(ch))));
	return out;
}

std::string encode(UStringView text, std::size_t* out_unmapped)
{
	std::string out;
	out.reserve(text.size());
	std::size_t unmapped = 0;
	for (char32_t c : text)
	{
		if (auto b = from_unicode(c))
			out.push_back(static_cast<char>(*b));
		else
		{
			out.push_back('?');
			++unmapped;
		}
	}
	if (out_unmapped != nullptr)
		*out_unmapped = unmapped;
	return out;
}

void word_key_into(std::string& out, UStringView word)
{
	out.clear();
	out.reserve(word.size());
	for (char32_t c : word)
	{
		if (auto b = from_unicode(c))
			out.push_back(static_cast<char>(*b));
		else
			out.push_back('?');
	}
}

std::string word_key(UStringView word)
{
	std::string key;
	word_key_into(key, word);
	return key;
}

void word_key_lower_into(std::string& out, UStringView word)
{
	word_key_into(out, word);
	lower_key_into(out, out);
}

void lower_key_into(std::string& out, std::string_view key)
{
	if (out.data() != key.data())
		out.assign(key);
	for (char& ch : out)
	{
		const auto b = static_cast<std::uint8_t>(static_cast<unsigned char>(ch));
		const char32_t c = to_unicode(b);
		if (const auto lo = from_unicode(cyr::to_lower(c)))
			ch = static_cast<char>(*lo);
	}
}

std::string lower_key(std::string_view key)
{
	std::string out;
	lower_key_into(out, key);
	return out;
}

} // namespace yo::cp1251
