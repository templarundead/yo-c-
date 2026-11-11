// Кодировки текста: определение, декодирование, потоковое чтение строк
// с сохранением исходных байтов перевода строки.
#pragma once

#include "yo/cyr.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <istream>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace yo {

enum class Encoding : std::uint8_t { cp1251, utf8, utf16le, utf16be };

struct EncodingInfo
{
	Encoding enc = Encoding::cp1251;
	bool bom = false;
};

// Параметры автоопределения (по умолчанию — как в оригинале: AutoUnicode=T,
// UTFLength=512 «слов» по 16 бит).
struct DetectOptions
{
	bool auto_unicode = true;
	unsigned utf_length_words = 512;
};

// Байты BOM для кодировки (пусто для cp1251).
[[nodiscard]] std::string_view bom_bytes(Encoding enc) noexcept;

[[nodiscard]] bool is_utf16(Encoding enc) noexcept;

// Определение кодировки по первым байтам файла. Повторяет UnicodeTextType
// оригинала, но дополнительно умеет UTF-16BE (оригинал читал его как ANSI).
[[nodiscard]] EncodingInfo detect_encoding(std::span<const std::uint8_t> head,
										   const DetectOptions& opts) noexcept;

// RTF определяется по расширению (как в оригинале).
[[nodiscard]] bool is_rtf_path(const std::filesystem::path& path);

// Декодирование одной строки (без перевода строки).
[[nodiscard]] UString decode_line(std::span<const std::uint8_t> bytes, Encoding enc);

// Кодирование без BOM. Непредставимые символы -> '?' (счётчик в out_unmapped).
[[nodiscard]] std::string encode_line(UStringView text, Encoding enc,
									  std::size_t* out_unmapped = nullptr);

// Потоковое чтение строк: буфер фиксированного размера, без загрузки файла
// целиком. Возвращает строку без EOL и исходные байты EOL.
class LineReader
{
public:
	LineReader(std::unique_ptr<std::istream> in, Encoding enc, std::size_t chunk = 1u << 20);

	// false — конец файла.
	bool next(UString& line, std::string& eol);

private:
	bool refill();
	bool next_single_byte(UString& line, std::string& eol);
	bool next_utf16(UString& line, std::string& eol);

	std::unique_ptr<std::istream> in_;
	Encoding enc_;
	std::size_t chunk_;
	std::string buf_;
	std::size_t pos_ = 0;
	bool eof_ = false;
};

// Запись строк в кодировке с сохранением EOL.
class LineWriter
{
public:
	LineWriter(std::ostream& out, Encoding enc, bool bom);

	void write(std::string_view bytes);
	void write_line(UStringView line, std::string_view eol);
	void write_bom_once();

private:
	std::ostream& out_;
	Encoding enc_;
	bool bom_;
	bool bom_written_ = false;
};

// Открытие файла для чтения (бинарно, без BOM-сдвига: BOM остаётся в потоке).
[[nodiscard]] std::unique_ptr<std::istream> open_binary(const std::filesystem::path& path);

// Чтение первых N байт файла (для автоопределения).
[[nodiscard]] std::string read_head(const std::filesystem::path& path, std::size_t n);

} // namespace yo
