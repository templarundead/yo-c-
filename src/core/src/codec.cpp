#include "yo/codec.hpp"

#include "yo/cp1251.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstring>
#include <fstream>
#include <limits>

namespace yo {

namespace {

constexpr char32_t kReplacement = 0xFFFD;

void append_utf8(std::string& out, char32_t c)
{
	if (c < 0x80)
		out.push_back(static_cast<char>(c));
	else if (c < 0x800)
	{
		out.push_back(static_cast<char>(0xC0 | (c >> 6)));
		out.push_back(static_cast<char>(0x80 | (c & 0x3F)));
	}
	else if (c < 0x10000)
	{
		out.push_back(static_cast<char>(0xE0 | (c >> 12)));
		out.push_back(static_cast<char>(0x80 | ((c >> 6) & 0x3F)));
		out.push_back(static_cast<char>(0x80 | (c & 0x3F)));
	}
	else
	{
		out.push_back(static_cast<char>(0xF0 | (c >> 18)));
		out.push_back(static_cast<char>(0x80 | ((c >> 12) & 0x3F)));
		out.push_back(static_cast<char>(0x80 | ((c >> 6) & 0x3F)));
		out.push_back(static_cast<char>(0x80 | (c & 0x3F)));
	}
}

// Строгий UTF-8: некорректные последовательности -> U+FFFD (по одной на байт).
UString utf8_decode(std::span<const std::uint8_t> bytes)
{
	UString out;
	out.reserve(bytes.size());
	std::size_t i = 0;
	const std::size_t n = bytes.size();
	while (i < n)
	{
		const std::uint8_t b = bytes[i];
		if (b < 0x80)
		{
			out.push_back(b);
			++i;
			continue;
		}
		std::size_t len = 0;
		char32_t cp = 0;
		if ((b & 0xE0) == 0xC0)
		{
			len = 2;
			cp = b & 0x1F;
		}
		else if ((b & 0xF0) == 0xE0)
		{
			len = 3;
			cp = b & 0x0F;
		}
		else if ((b & 0xF8) == 0xF0)
		{
			len = 4;
			cp = b & 0x07;
		}
		else
		{
			out.push_back(kReplacement);
			++i;
			continue;
		}
		if (i + len > n)
		{
			out.push_back(kReplacement);
			++i;
			continue;
		}
		bool ok = true;
		for (std::size_t k = 1; k < len; ++k)
		{
			const std::uint8_t cb = bytes[i + k];
			if ((cb & 0xC0) != 0x80)
			{
				ok = false;
				break;
			}
			cp = (cp << 6) | (cb & 0x3F);
		}
		const bool overlong = (len == 2 && cp < 0x80) || (len == 3 && cp < 0x800) ||
							  (len == 4 && cp < 0x10000);
		if (!ok || overlong || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF))
		{
			out.push_back(kReplacement);
			++i;
			continue;
		}
		out.push_back(cp);
		i += len;
	}
	return out;
}

char32_t unit_to_cp(std::uint8_t lo, std::uint8_t hi, Encoding enc)
{
	return enc == Encoding::utf16le ? static_cast<char32_t>(lo | (hi << 8))
									: static_cast<char32_t>((lo << 8) | hi);
}

} // namespace

std::string_view bom_bytes(Encoding enc) noexcept
{
	switch (enc)
	{
	case Encoding::utf8:
		return "\xEF\xBB\xBF";
	case Encoding::utf16le:
		return "\xFF\xFE";
	case Encoding::utf16be:
		return "\xFE\xFF";
	case Encoding::cp1251:
		break;
	}
	return {};
}

bool is_utf16(Encoding enc) noexcept
{
	return enc == Encoding::utf16le || enc == Encoding::utf16be;
}

bool is_rtf_path(const std::filesystem::path& path)
{
	std::string ext = path.extension().string();
	for (char& ch : ext)
		ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
	return ext == ".RTF";
}

EncodingInfo detect_encoding(std::span<const std::uint8_t> head, const DetectOptions& opts) noexcept
{
	const std::size_t n = head.size();
	if (n >= 2)
	{
		const auto b0 = head[0];
		const auto b1 = head[1];
		const unsigned w = static_cast<unsigned>(b0) | (static_cast<unsigned>(b1) << 8);
		if (w == 0xFEFF)
			return { Encoding::utf16le, true };
		if (w == 0xFFFE)
			return { Encoding::utf16be, true };
		if (n >= 3 && w == 0xBBEF && head[2] == 0xBF)
			return { Encoding::utf8, true };
	}

	// Эвристика оригинала (AutoUnicode): счётчики по 16-битным «словам».
	const unsigned limit = std::max(1u, opts.utf_length_words);
	long u16h = 0, nu16h = 0, u16l = 0, nu16l = 0, u8 = 0;
	std::string sample;
	long i = 0;
	for (std::size_t p = 0; p + 1 < n; p += 2)
	{
		const auto lo = head[p];
		const auto hi = head[p + 1];
		++i;
		if ((hi == 0x00) || (hi == 0x04))
			++u16h;
		else
			++nu16h;
		if ((lo == 0x00) || (lo == 0x04))
			++u16l;
		else
			++nu16l;
		// Порядок как в оригинале: сначала младший байт (Lo), затем старший (Hi).
		if (lo != 13)
			sample.push_back(static_cast<char>(lo));
		else if (!opts.auto_unicode)
			break;
		if (hi != 13)
			sample.push_back(static_cast<char>(hi));
		else if (!opts.auto_unicode)
			break;
		if ((hi & 0xFE) == 0xD0)
			++u8;
		else if ((lo & 0xFE) == 0xD0)
			++u8;
		if (static_cast<unsigned>(i) >= limit)
			break;
	}
	(void)i;

	std::string lowered;
	lowered.reserve(sample.size());
	for (char ch : sample)
		lowered.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(ch))));
	if (lowered.find("encoding=\"utf-8\"") != std::string::npos)
		return { Encoding::utf8, false };

	if (opts.auto_unicode)
	{
		if (u8 > 20)
			return { Encoding::utf8, false };
		if (u16h >= u16l && u16h > nu16h)
			return { Encoding::utf16le, false };
		if (u16l > nu16l)
			return { Encoding::utf16be, false };
	}
	return { Encoding::cp1251, false };
}

UString decode_line(std::span<const std::uint8_t> bytes, Encoding enc)
{
	switch (enc)
	{
	case Encoding::cp1251:
	{
		UString out;
		out.reserve(bytes.size());
		for (auto b : bytes)
			out.push_back(cp1251::to_unicode(b));
		return out;
	}
	case Encoding::utf8:
		return utf8_decode(bytes);
	case Encoding::utf16le:
	case Encoding::utf16be:
	{
		UString out;
		out.reserve(bytes.size() / 2);
		std::size_t i = 0;
		while (i + 1 < bytes.size())
		{
			const char32_t cp = unit_to_cp(bytes[i], bytes[i + 1], enc);
			i += 2;
			if (cp >= 0xD800 && cp <= 0xDBFF && i + 1 < bytes.size())
			{
				const char32_t lo = unit_to_cp(bytes[i], bytes[i + 1], enc);
				if (lo >= 0xDC00 && lo <= 0xDFFF)
				{
					i += 2;
					out.push_back(0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00));
					continue;
				}
			}
			out.push_back((cp >= 0xD800 && cp <= 0xDFFF) ? kReplacement : cp);
		}
		return out;
	}
	}
	return {};
}

std::string encode_line(UStringView text, Encoding enc, std::size_t* out_unmapped)
{
	std::size_t unmapped = 0;
	std::string out;
	switch (enc)
	{
	case Encoding::cp1251:
		out = cp1251::encode(text, &unmapped);
		break;
	case Encoding::utf8:
		out.reserve(text.size() * 2);
		for (char32_t c : text)
			append_utf8(out, c);
		break;
	case Encoding::utf16le:
	case Encoding::utf16be:
		out.reserve(text.size() * 2);
		for (char32_t c : text)
		{
			auto put = [&](char32_t u)
			{
				if (enc == Encoding::utf16le)
				{
					out.push_back(static_cast<char>(u & 0xFF));
					out.push_back(static_cast<char>((u >> 8) & 0xFF));
				}
				else
				{
					out.push_back(static_cast<char>((u >> 8) & 0xFF));
					out.push_back(static_cast<char>(u & 0xFF));
				}
			};
			if (c > 0x10FFFF || (c >= 0xD800 && c <= 0xDFFF))
			{
				put(static_cast<char32_t>(U'?'));
				++unmapped;
			}
			else if (c >= 0x10000)
			{
				const char32_t v = c - 0x10000;
				put(0xD800 + (v >> 10));
				put(0xDC00 + (v & 0x3FF));
			}
			else
				put(c);
		}
		break;
	}
	if (out_unmapped != nullptr)
		*out_unmapped = unmapped;
	return out;
}

LineReader::LineReader(std::unique_ptr<std::istream> in, Encoding enc, std::size_t chunk)
	: in_(std::move(in)), enc_(enc), chunk_(chunk)
{
	buf_.reserve(chunk_);
}

bool LineReader::refill()
{
	if (eof_ || in_ == nullptr)
		return false;
	if (pos_ > 0)
	{
		buf_.erase(0, pos_);
		pos_ = 0;
	}
	const std::size_t old = buf_.size();
	buf_.resize(old + chunk_);
	in_->read(buf_.data() + static_cast<std::ptrdiff_t>(old), static_cast<std::streamsize>(chunk_));
	const auto got = static_cast<std::size_t>(in_->gcount());
	buf_.resize(old + got);
	if (got == 0)
		eof_ = true;
	return got != 0;
}

bool LineReader::next(UString& line, std::string& eol)
{
	return is_utf16(enc_) ? next_utf16(line, eol) : next_single_byte(line, eol);
}

bool LineReader::next_single_byte(UString& line, std::string& eol)
{
	for (;;)
	{
		const std::size_t n = buf_.size();
		std::size_t i = pos_;
		while (i < n && buf_[i] != '\n' && buf_[i] != '\r')
			++i;
		if (i < n)
		{
			if (buf_[i] == '\r' && i + 1 >= n && !eof_)
			{
				// Нужен следующий байт, чтобы отличить CRLF от CR:
				// после refill() буфер (и индексы) пересчитываются заново.
				refill();
				continue;
			}
			const auto* begin = reinterpret_cast<const std::uint8_t*>(buf_.data());
			line = decode_line({ begin + pos_, i - pos_ }, enc_);
			if (buf_[i] == '\r')
			{
				if (i + 1 < buf_.size() && buf_[i + 1] == '\n')
				{
					eol = "\r\n";
					pos_ = i + 2;
				}
				else
				{
					eol = "\r";
					pos_ = i + 1;
				}
			}
			else
			{
				eol = "\n";
				pos_ = i + 1;
			}
			return true;
		}
		// Перевода строки нет в буфере.
		if (eof_)
		{
			if (pos_ < n)
			{
				const auto* begin = reinterpret_cast<const std::uint8_t*>(buf_.data());
				line = decode_line({ begin + pos_, n - pos_ }, enc_);
				eol.clear();
				pos_ = n;
				return true;
			}
			return false;
		}
		if (!refill() && pos_ >= buf_.size())
			return false;
	}
}

bool LineReader::next_utf16(UString& line, std::string& eol)
{
	line.clear();
	eol.clear();
	for (;;)
	{
		const std::size_t n = buf_.size();
		std::size_t i = pos_;
		bool found = false;
		char32_t term = 0;
		while (i + 1 < n)
		{
			const char32_t cp = unit_to_cp(static_cast<std::uint8_t>(buf_[i]),
										   static_cast<std::uint8_t>(buf_[i + 1]), enc_);
			if (cp == 0x0A || cp == 0x0D)
			{
				found = true;
				term = cp;
				break;
			}
			i += 2;
		}
		if (found)
		{
			if (term == 0x0D && i + 3 >= n && !eof_)
			{
				refill();
				continue;
			}
			line = decode_line({ reinterpret_cast<const std::uint8_t*>(buf_.data()) + pos_,
								 i - pos_ },
							   enc_);
			if (term == 0x0D && i + 3 < buf_.size())
			{
				const char32_t next_cp = unit_to_cp(static_cast<std::uint8_t>(buf_[i + 2]),
													static_cast<std::uint8_t>(buf_[i + 3]), enc_);
				if (next_cp == 0x0A)
				{
					eol = "\r\n";
					pos_ = i + 4;
					return true;
				}
			}
			eol = term == 0x0D ? "\r" : "\n";
			pos_ = i + 2;
			return true;
		}
		if (eof_)
		{
			if (pos_ + 1 < n)
			{
				line = decode_line({ reinterpret_cast<const std::uint8_t*>(buf_.data()) + pos_,
									 n - pos_ },
								   enc_);
				pos_ = n;
				return true;
			}
			return false;
		}
		if (!refill() && pos_ + 1 >= buf_.size())
			return false;
	}
}

LineWriter::LineWriter(std::ostream& out, Encoding enc, bool bom)
	: out_(out), enc_(enc), bom_(bom)
{
}

void LineWriter::write(std::string_view bytes)
{
	write_bom_once();
	out_.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
}

void LineWriter::write_bom_once()
{
	if (!bom_ || bom_written_)
		return;
	auto b = bom_bytes(enc_);
	out_.write(b.data(), static_cast<std::streamsize>(b.size()));
	bom_written_ = true;
}

void LineWriter::write_line(UStringView line, std::string_view eol)
{
	write_bom_once();
	const std::string bytes = encode_line(line, enc_);
	out_.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
	// Перевод строки тоже пишется в целевой кодировке: для UTF-16 это
	// двухбайтовые коды, иначе в файле получаются одиночные 0x0D/0x0A.
	if (enc_ == Encoding::utf16le || enc_ == Encoding::utf16be)
	{
		char utf16_eol[8] = {};
		std::size_t n = 0;
		for (const char c : eol)
		{
			if (n + 2 > sizeof utf16_eol)
				break;
			if (enc_ == Encoding::utf16le)
			{
				utf16_eol[n++] = c;
				utf16_eol[n++] = '\0';
			}
			else
			{
				utf16_eol[n++] = '\0';
				utf16_eol[n++] = c;
			}
		}
		out_.write(utf16_eol, static_cast<std::streamsize>(n));
		return;
	}
	out_.write(eol.data(), static_cast<std::streamsize>(eol.size()));
}

std::unique_ptr<std::istream> open_binary(const std::filesystem::path& path)
{
	auto in = std::make_unique<std::ifstream>(path, std::ios::binary);
	if (!*in)
		return nullptr;
	return in;
}

std::string read_head(const std::filesystem::path& path, std::size_t n)
{
	std::ifstream in(path, std::ios::binary);
	if (!in)
		return {};
	std::string head(n, '\0');
	in.read(head.data(), static_cast<std::streamsize>(n));
	head.resize(static_cast<std::size_t>(in.gcount()));
	return head;
}

} // namespace yo
