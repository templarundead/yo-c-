#include "yo/utf.hpp"

#include <cstdint>

namespace yo {

namespace {

constexpr char16_t kReplacement = 0xFFFD;

} // namespace

std::size_t utf8_char_len(char c) noexcept
{
	const auto b = static_cast<unsigned char>(c);
	if (b < 0x80U)
		return 1;
	if ((b & 0xE0U) == 0xC0U)
		return 2;
	if ((b & 0xF0U) == 0xE0U)
		return 3;
	if ((b & 0xF8U) == 0xF0U)
		return 4;
	return 0;
}

std::u16string utf8_to_utf16(std::string_view utf8)
{
	std::u16string out;
	out.reserve(utf8.size());
	std::size_t i = 0;
	while (i < utf8.size())
	{
		const auto b0 = static_cast<unsigned char>(utf8[i]);
		if (b0 < 0x80U)
		{
			out.push_back(static_cast<char16_t>(b0));
			++i;
			continue;
		}
		const std::size_t len = utf8_char_len(utf8[i]);
		if (len < 2 || i + len > utf8.size())
		{
			out.push_back(kReplacement);
			++i;
			continue;
		}
		std::uint32_t cp = 0;
		bool ok = true;
		for (std::size_t k = 1; k < len; ++k)
		{
			const auto bk = static_cast<unsigned char>(utf8[i + k]);
			if ((bk & 0xC0U) != 0x80U)
			{
				ok = false;
				break;
			}
			cp = (cp << 6) | (bk & 0x3FU);
		}
		if (!ok)
		{
			out.push_back(kReplacement);
			++i;
			continue;
		}
		switch (len)
		{
		case 2:
			cp |= static_cast<std::uint32_t>(b0 & 0x1FU) << 6;
			break;
		case 3:
			cp |= static_cast<std::uint32_t>(b0 & 0x0FU) << 12;
			break;
		default:
			cp |= static_cast<std::uint32_t>(b0 & 0x07U) << 18;
			break;
		}
		i += len;
		if (cp > 0x10FFFFU || (cp >= 0xD800U && cp <= 0xDFFFU))
		{
			out.push_back(kReplacement);
			continue;
		}
		if (cp >= 0x10000U)
		{
			cp -= 0x10000U;
			out.push_back(static_cast<char16_t>(0xD800U + (cp >> 10)));
			out.push_back(static_cast<char16_t>(0xDC00U + (cp & 0x3FFU)));
		}
		else
		{
			out.push_back(static_cast<char16_t>(cp));
		}
	}
	return out;
}

std::string utf16_to_utf8(std::u16string_view utf16)
{
	std::string out;
	out.reserve(utf16.size() * 3 / 2 + 1);
	for (std::size_t i = 0; i < utf16.size(); ++i)
	{
		std::uint32_t cp = utf16[i];
		if (cp >= 0xD800U && cp <= 0xDBFFU && i + 1 < utf16.size())
		{
			const std::uint32_t lo = utf16[i + 1];
			if (lo >= 0xDC00U && lo <= 0xDFFFU)
			{
				cp = 0x10000U + ((cp - 0xD800U) << 10) + (lo - 0xDC00U);
				++i;
			}
			else
			{
				cp = kReplacement;
			}
		}
		else if (cp >= 0xD800U && cp <= 0xDFFFU)
		{
			cp = kReplacement;
		}

		if (cp < 0x80U)
			out.push_back(static_cast<char>(cp));
		else if (cp < 0x800U)
		{
			out.push_back(static_cast<char>(0xC0U | (cp >> 6)));
			out.push_back(static_cast<char>(0x80U | (cp & 0x3FU)));
		}
		else if (cp < 0x10000U)
		{
			out.push_back(static_cast<char>(0xE0U | (cp >> 12)));
			out.push_back(static_cast<char>(0x80U | ((cp >> 6) & 0x3FU)));
			out.push_back(static_cast<char>(0x80U | (cp & 0x3FU)));
		}
		else
		{
			out.push_back(static_cast<char>(0xF0U | (cp >> 18)));
			out.push_back(static_cast<char>(0x80U | ((cp >> 12) & 0x3FU)));
			out.push_back(static_cast<char>(0x80U | ((cp >> 6) & 0x3FU)));
			out.push_back(static_cast<char>(0x80U | (cp & 0x3FU)));
		}
	}
	return out;
}

bool utf8_valid(std::string_view utf8) noexcept
{
	std::size_t i = 0;
	while (i < utf8.size())
	{
		const auto b0 = static_cast<unsigned char>(utf8[i]);
		if (b0 < 0x80U)
		{
			++i;
			continue;
		}
		const std::size_t len = utf8_char_len(utf8[i]);
		if (len < 2 || i + len > utf8.size())
			return false;
		std::uint32_t cp = 0;
		for (std::size_t k = 1; k < len; ++k)
		{
			const auto bk = static_cast<unsigned char>(utf8[i + k]);
			if ((bk & 0xC0U) != 0x80U)
				return false;
			cp = (cp << 6) | (bk & 0x3FU);
		}
		switch (len)
		{
		case 2:
			cp |= static_cast<std::uint32_t>(b0 & 0x1FU) << 6;
			break;
		case 3:
			cp |= static_cast<std::uint32_t>(b0 & 0x0FU) << 12;
			break;
		default:
			cp |= static_cast<std::uint32_t>(b0 & 0x07U) << 18;
			break;
		}
		if (cp > 0x10FFFFU || (cp >= 0xD800U && cp <= 0xDFFFU))
			return false;
		i += len;
	}
	return true;
}

std::string_view eol_bytes(Eol eol) noexcept
{
	switch (eol)
	{
	case Eol::lf:
		return "\n";
	case Eol::crlf:
		return "\r\n";
	case Eol::cr:
		return "\r";
	case Eol::none:
	default:
		return {};
	}
}

std::string normalize_newlines(std::string_view text, Eol eol)
{
	const std::string_view target = eol_bytes(eol == Eol::none ? Eol::lf : eol);
	std::string out;
	out.reserve(text.size() + text.size() / 16 + 1);
	for (std::size_t i = 0; i < text.size(); ++i)
	{
		const char c = text[i];
		if (c == '\r')
		{
			if (i + 1 < text.size() && text[i + 1] == '\n')
				++i;
			out.append(target);
			continue;
		}
		if (c == '\n')
		{
			out.append(target);
			continue;
		}
		out.push_back(c);
	}
	return out;
}

#ifdef _WIN32
std::wstring utf8_to_wide(std::string_view utf8)
{
	const std::u16string u16 = utf8_to_utf16(utf8);
	return std::wstring(u16.begin(), u16.end());
}

std::string wide_to_utf8(std::wstring_view wide)
{
	const std::u16string u16(wide.begin(), wide.end());
	return utf16_to_utf8(u16);
}
#endif

} // namespace yo
