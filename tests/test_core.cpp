// Юнит-тесты базовых слоёв: кириллица, CP1251, кодировки, словари, РВ.

#include "yo/codec.hpp"
#include "yo/cp1251.hpp"
#include "yo/cyr.hpp"
#include "yo/dict.hpp"
#include "yo/regexdict.hpp"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <sstream>
#include <string>
#include <vector>

using namespace yo;

namespace {

const std::filesystem::path kDataDir = YO_DATA_DIR;

// Литералы в тестах — UTF-8 (кодировка исходников).
UString u(std::string_view utf8)
{
	return decode_line({ reinterpret_cast<const std::uint8_t*>(utf8.data()), utf8.size() },
					   Encoding::utf8);
}

std::string enc(UStringView s) { return encode_line(s, Encoding::utf8); }

std::string key(std::string_view utf8) { return cp1251::word_key(u(utf8)); }

Dictionary& shared_dict()
{
	static Dictionary dict = []
	{
		std::vector<LoadIssue> issues;
		auto d = Dictionary::load(kDataDir / "yox.dat", &issues);
		REQUIRE(d.has_value());
		return std::move(*d);
	}();
	return dict;
}

} // namespace

TEST_CASE("кириллица: регистр и классы как в оригинале", "[cyr]")
{
	CHECK(cyr::is_lower(U'а'));
	CHECK(cyr::is_lower(U'я'));
	CHECK(cyr::is_lower(U'ё'));
	CHECK(cyr::is_upper(U'А'));
	CHECK(cyr::is_upper(U'Ё'));
	CHECK_FALSE(cyr::is_alpha(U'a')); // латиница не буква для токенизатора
	CHECK_FALSE(cyr::is_alpha(U'5'));
	CHECK(cyr::to_lower(U'Ё') == U'ё');
	CHECK(cyr::to_lower(U'Я') == U'я');
	CHECK(cyr::to_lower(U'A') == U'a');
	CHECK(cyr::to_upper(U'ё') == U'Ё');
	CHECK(cyr::is_word_char(U'_') == false); // в MyWordChars нет '_'
	CHECK(cyr::is_word_char(U'5'));
	CHECK(cyr::is_word_char(U'z'));
	CHECK(cyr::is_word_char(U'ё'));
}

TEST_CASE("ToYe и CopyCase", "[cyr]")
{
	CHECK(enc(cyr::to_ye(u("всё ещё"))) == "все еще");
	CHECK(enc(cyr::to_ye(u("ВСЁ"))) == "ВСЕ");
	CHECK(enc(cyr::copy_case(u("все"), u("ВСЕ"))) == "ВСЕ");
	CHECK(enc(cyr::copy_case(u("Всё"), u("все"))) == "Всё");
	CHECK(enc(cyr::copy_case(u("всё"), u("Мы"))) == "Всё");
	CHECK(cyr::is_ye_yo_if(u("шел"), true));
	CHECK_FALSE(cyr::is_ye_yo_if(u("шёл"), false));
	CHECK(cyr::is_ye_yo_if(u("шёл"), true));
}

TEST_CASE("CP1251: обратимость и порядок байтов", "[cp1251]")
{
	for (int b = 0; b < 256; ++b)
	{
		const auto ch = static_cast<std::uint8_t>(b);
		const char32_t cp = cp1251::to_unicode(ch);
		const auto back = cp1251::from_unicode(cp);
		REQUIRE(back.has_value());
		CHECK(*back == ch);
	}
	// ё (0xB8) в байтовом порядке CP1251 меньше е (0xE5) — на этом держится
	// сортировка словаря.
	CHECK(key("ё") < key("е"));
	CHECK(cp1251::lower_key(key("ВСЁ")) == key("всё"));
}

TEST_CASE("определение кодировки", "[codec]")
{
	auto span_of = [](std::string_view s)
	{
		return std::span<const std::uint8_t>(reinterpret_cast<const std::uint8_t*>(s.data()),
											 s.size());
	};
	DetectOptions opts;
	CHECK(detect_encoding(span_of("\xEF\xBB\xBFпривет"), opts).enc == Encoding::utf8);
	CHECK(detect_encoding(span_of("\xEF\xBB\xBFпривет"), opts).bom);
	CHECK(detect_encoding(span_of("\xFF\xFE\x00\x04"), opts).enc == Encoding::utf16le);
	CHECK(detect_encoding(span_of("\xFE\xFF\x04\x00"), opts).enc == Encoding::utf16be);
	// UTF-8 без BOM: много байтов D0/D1.
	const std::string utf8 = "Съешь ещё этих мягких французских булок";
	CHECK(detect_encoding(span_of(utf8), opts).enc == Encoding::utf8);
	// CP1251: байты D0/D1 встречаются редко, а пар с 0x04/0x00 нет.
	const std::string cp = cp1251::encode(u("Простой русский текст без ёмких слов"));
	CHECK(detect_encoding(span_of(cp), opts).enc == Encoding::cp1251);
	// Атрибут XML объявляет UTF-8.
	CHECK(detect_encoding(span_of("<?xml encoding=\"utf-8\"?>"), opts).enc == Encoding::utf8);
}

TEST_CASE("декодирование UTF-8: ошибки заменяются U+FFFD", "[codec]")
{
	const std::string bad = "аб\xFF\xC3";
	const UString decoded = decode_line(
		{ reinterpret_cast<const std::uint8_t*>(bad.data()), bad.size() }, Encoding::utf8);
	CHECK(decoded.starts_with(U"аб"));
	CHECK(decoded.back() == U'\uFFFD');
}

TEST_CASE("потоковое чтение сохраняет CRLF/LF/CR и BOM", "[codec]")
{
	auto run = [](std::string bytes, Encoding enc, bool bom)
	{
		auto in = std::make_unique<std::istringstream>(bytes, std::ios::in | std::ios::binary);
		LineReader reader(std::move(in), enc);
		std::ostringstream out;
		LineWriter writer(out, enc, bom);
		UString line;
		std::string eol;
		while (reader.next(line, eol))
			writer.write_line(line, eol);
		return out.str();
	};

	const std::string crlf = "один\r\nдва\r\n";
	CHECK(run(crlf, Encoding::cp1251, false) == crlf);

	const std::string lf = "один\nдва\n";
	CHECK(run(lf, Encoding::cp1251, false) == lf);

	const std::string cr = "один\rдва\r";
	CHECK(run(cr, Encoding::cp1251, false) == cr);

	const std::string utf8_body = encode_line(u("один\nдва"), Encoding::utf8);
	const std::string with_bom = "\xEF\xBB\xBF" + utf8_body;
	CHECK(run(with_bom.substr(3), Encoding::utf8, true) == with_bom);

	// Без завершающего перевода строки.
	const std::string no_eol = "один\nдва";
	CHECK(run(no_eol, Encoding::cp1251, false) == no_eol);

	// UTF-16LE.
	auto in16 = std::make_unique<std::istringstream>(
		encode_line(u("один\nдва\n"), Encoding::utf16le), std::ios::in | std::ios::binary);
	LineReader r16(std::move(in16), Encoding::utf16le);
	UString line;
	std::string eol;
	REQUIRE(r16.next(line, eol));
	CHECK(line == u("один"));
	CHECK(eol == "\n");
}

TEST_CASE("запись строк: перевод строки в кодировке файла", "[codec]")
{
	// UTF-16: EOL пишется двухбайтовыми кодами, иначе в файле окажутся
	// одиночные 0x0D/0x0A (баг, найденный на UTF-16LE-файле).
	const UString line = u("ab");

	std::ostringstream le;
	LineWriter wle(le, Encoding::utf16le, false);
	wle.write_line(line, "\r\n");
	const std::string le_bytes = le.str();
	REQUIRE(le_bytes.size() == 8u);
	CHECK(static_cast<unsigned char>(le_bytes[4]) == 0x0D);
	CHECK(static_cast<unsigned char>(le_bytes[5]) == 0x00);
	CHECK(static_cast<unsigned char>(le_bytes[6]) == 0x0A);
	CHECK(static_cast<unsigned char>(le_bytes[7]) == 0x00);

	std::ostringstream be;
	LineWriter wbe(be, Encoding::utf16be, false);
	wbe.write_line(line, "\n");
	const std::string be_bytes = be.str();
	REQUIRE(be_bytes.size() == 6u);
	CHECK(static_cast<unsigned char>(be_bytes[4]) == 0x00);
	CHECK(static_cast<unsigned char>(be_bytes[5]) == 0x0A);

	// Однобайтовые кодировки: EOL как есть.
	std::ostringstream cp;
	LineWriter wcp(cp, Encoding::cp1251, false);
	wcp.write_line(line, "\r\n");
	CHECK(cp.str() == std::string("ab\r\n"));
}

TEST_CASE("словарь yox.dat загружается без нарушений", "[dict]")
{
	std::vector<LoadIssue> issues;
	const auto dict = Dictionary::load(kDataDir / "yox.dat", &issues);
	REQUIRE(dict.has_value());
	CHECK(issues.empty());
	CHECK(dict->size() == 140324);
	CHECK(dict->version() == "1.502");
	// Точный размер исходника: ловит порчу файла при копировании
	// (например, перекодировку CP1251 → UTF-8 с заменой на U+FFFD).
	CHECK(std::filesystem::file_size(kDataDir / "yox.dat") == 1865705);
}

TEST_CASE("поиск словоформы: е и ё эквивалентны, регистр не важен", "[dict]")
{
	auto& dict = shared_dict();
	// Ключи словаря — только е-формы (ToYe); запрос с ё не находится
	// напрямую, как и в оригинале (SelectWord всегда передаёт ToYe).
	const int i1 = dict.find_key(key("все"));
	const int i2 = dict.find_key(key("всё"));
	const int i3 = dict.find_key(key("Все"));
	REQUIRE(i1 >= 0);
	CHECK(i1 == i3);
	CHECK(i2 == -1);
	CHECK(enc(dict.yo_unicode(static_cast<std::size_t>(i1))) == "всё");
	CHECK(dict.find_key(key("нетакогослова")) == -1);
	CHECK(dict.find_key(key("шел")) >= 0);
	CHECK(dict.find_key(key("шёл")) == -1);
}

TEST_CASE("дубли словаря разрешаются как в оригинале", "[dict]")
{
	auto& dict = shared_dict();
	// Аксенова/Аксёнова — 360 групп после нормализации; обе формы находят
	// одну и ту же запись через бинарный поиск оригинала.
	const int a = dict.find_key(key("Аксенова"));
	const int b = dict.find_key(key("АКСЕНОВА"));
	REQUIRE(a >= 0);
	REQUIRE(b >= 0);
	CHECK(a == b);
}

TEST_CASE("поиск словоформы: индекс согласован со всем словарём", "[dict]")
{
	auto& dict = shared_dict();
	const std::size_t n = dict.size();
	REQUIRE(n > 100000);
	std::size_t checked = 0;
	for (std::size_t i = 0; i < n; i += 97)
	{
		const UString ye = dict.ye_unicode(i);
		const std::string lc = cp1251::word_key(cyr::to_lower(ye));
		const int f = dict.find_key(lc);
		REQUIRE(f >= 0);
		// Дубликаты LC-ключей разрешаются двоичным поиском оригинала,
		// поэтому сверяем ключ, а не номер записи.
		CHECK(dict.ye_lc(static_cast<std::size_t>(f)) == dict.ye_lc(i));

		UString cap = ye;
		cap[0] = cyr::to_upper(cap[0]);
		CHECK(dict.find(cap) >= 0);

		UString upper = ye;
		for (auto& c : upper)
			c = cyr::to_upper(c);
		CHECK(dict.find(upper) >= 0);
		++checked;
	}
	CHECK(checked > 1000);
}

TEST_CASE("словарь регулярных выражений: 94 правила компилируются", "[regex]")
{
	std::vector<LoadIssue> issues;
	const auto rd = RegexDictionary::load(kDataDir / "yo_regexpr.dat", &issues);
	REQUIRE(rd.has_value());
	CHECK(rd->size() == 94);
	CHECK(rd->version() == "1.09");
	// Словарь РВ в поставке — CP1251 (в апстриме файл в UTF-8,
	// см. tools/fetch_data.py).
	CHECK(std::filesystem::file_size(kDataDir / "yo_regexpr.dat") == 1186);
	CHECK(issues.empty());
	for (const auto& rule : rd->rules())
		CHECK(rule.re->ok());
}

TEST_CASE("MakeValidRegExpr повторяет оригинал", "[regex]")
{
	// Пробелы -> \s+, е/ё -> ([её]), '-' -> -(\r\n){0,1}, краевые \b.
	CHECK(enc(make_valid_regexp(u("всё время"))) == "\\bвс([её])\\s+вр([её])мя\\b");
	CHECK(enc(make_valid_regexp(u("всё-таки"))) == "\\bвс([её])-(\\r\\n){0,1}таки\\b");
	CHECK(enc(make_valid_regexp(u("всем"))) == "\\bвс([её])м\\b");
	CHECK(enc(make_valid_regexp(u("\\w+х лет"))) == "\\w+х\\s+л([её])т\\b");
	// Внутри класса е/ё не превращаются в группы.
	CHECK(enc(make_valid_regexp(u("[а-я]+"))) == "[а-я]+");
}

TEST_CASE("транслятор РВ: классы WordChars/SpaceChars", "[regex]")
{
	RuleBuildInfo info;
	const std::string pattern = translate_to_re2(u("\\w+\\s\\S\\d"), info);
	CHECK(pattern.find("0-9A-Za-zА-Яа-яЁё") != std::string::npos);
	CHECK(pattern.find("[ \\t\\n\\r\\f\\x{A0}]") != std::string::npos);
	CHECK(pattern.find("[^ \\t\\n\\r\\f\\x{A0}]") != std::string::npos);
	CHECK(info.warnings.empty());

	RuleBuildInfo info2;
	const std::string p2 = translate_to_re2(u("\\bдом\\b"), info2);
	CHECK(info2.lead_boundary);
	CHECK(info2.trail_boundary);
	CHECK(p2 == "дом");

	RuleBuildInfo info3;
	(void)translate_to_re2(u("а\\bб"), info3);
	CHECK(!info3.warnings.empty()); // \b внутри шаблона — документированное отклонение
}

TEST_CASE("правило РВ срабатывает на контексте", "[regex]")
{
	std::vector<LoadIssue> issues;
	auto rd = RegexDictionary::load(kDataDir / "yo_regexpr.dat", &issues);
	REQUIRE(rd.has_value());

	// "все равно" -> правило «всё равно» меняет слово на вариант с ё.
	const UString ctx = u("Он все равно придет.");
	const UString w1 = u("все");
	const UString w2 = u("всё");
	int word_n = 0;
	std::size_t rule = 0;
	REQUIRE(rd->lookup(ctx, 3, w1, w2, word_n, rule));
	CHECK(word_n == 2);

	// "Всем известно" -> «всем» (без ё).
	const UString ctx2 = u("Всем известно, что он не пришел.");
	int word_n2 = 0;
	std::size_t rule2 = 0;
	REQUIRE(rd->lookup(ctx2, 0, u("Всем"), u("Всём"), word_n2, rule2));
	CHECK(word_n2 == 1);
}
