// Тесты документного буфера: piece table, индекс строк, undo/redo, mmap.
//
// Инвариант: содержимое и разбиение на строки совпадают с эталонной
// моделью на std::string при произвольной последовательности правок.

#include "yo/mapped.hpp"
#include "yo/textbuf.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstdio>
#include <filesystem>
#include <string>
#include <utility>
#include <vector>

using namespace yo;

namespace {

std::shared_ptr<MemorySource> mem(std::string s)
{
	return std::make_shared<MemorySource>(std::move(s));
}

// Эталон: разбиение текста на строки теми же правилами (LF/CRLF/CR).
struct RefLine
{
	std::string text;
	Eol eol = Eol::none;
};

std::vector<RefLine> ref_split(const std::string& s)
{
	std::vector<RefLine> out;
	std::size_t i = 0;
	RefLine cur;
	while (i < s.size())
	{
		const char c = s[i];
		if (c == '\r')
		{
			if (i + 1 < s.size() && s[i + 1] == '\n')
			{
				cur.eol = Eol::crlf;
				i += 2;
			}
			else
			{
				cur.eol = Eol::cr;
				++i;
			}
			out.push_back(cur);
			cur = RefLine{};
			continue;
		}
		if (c == '\n')
		{
			cur.eol = Eol::lf;
			++i;
			out.push_back(cur);
			cur = RefLine{};
			continue;
		}
		cur.text.push_back(c);
		++i;
	}
	out.push_back(cur);
	return out;
}

void check_matches_reference(const TextBuffer& buf, const std::string& model, const char* what)
{
	INFO(what);
	CHECK(buf.text() == model);
	const auto ref = ref_split(model);
	REQUIRE(buf.line_count() == ref.size());
	std::size_t off = 0;
	for (std::size_t i = 0; i < ref.size(); ++i)
	{
		INFO("строка " << i);
		CHECK(buf.line_start(i) == off);
		CHECK(std::string(buf.line_text(i)) == ref[i].text);
		CHECK(buf.line_eol(i) == ref[i].eol);
		CHECK(buf.line_of(off) == i);
		off += ref[i].text.size();
		off += ref[i].eol == Eol::crlf ? 2 : (ref[i].eol == Eol::none ? 0 : 1);
	}
	CHECK(off == model.size());
}

// Детерминированный ГПСЧ (без зависимости от реализации <random>).
struct Rng
{
	std::uint64_t s = 0x1234567890abcdefull;
	std::uint64_t next()
	{
		s ^= s << 13;
		s ^= s >> 7;
		s ^= s << 17;
		return s;
	}
	std::size_t range(std::size_t n) { return n == 0 ? 0 : static_cast<std::size_t>(next() % n); }
};

} // namespace

TEST_CASE("пустой буфер: одна пустая строка", "[textbuf]")
{
	TextBuffer buf;
	CHECK(buf.size() == 0);
	CHECK(buf.line_count() == 1);
	CHECK(buf.line_text(0).empty());
	CHECK(buf.line_eol(0) == Eol::none);
	CHECK(buf.line_of(0) == 0);
	CHECK(!buf.modified());
}

TEST_CASE("строки и переводы строк: LF, CRLF, CR", "[textbuf]")
{
	SECTION("LF с завершающим переводом")
	{
		const std::string src = "abc\ndef\n";
		TextBuffer buf(mem(src));
		check_matches_reference(buf, src, "LF");
		CHECK(buf.line_count() == 3);
		CHECK(buf.line_eol(0) == Eol::lf);
		CHECK(buf.line_eol(2) == Eol::none);
	}
	SECTION("CRLF без завершающего перевода")
	{
		const std::string src = "a\r\nb";
		TextBuffer buf(mem(src));
		check_matches_reference(buf, src, "CRLF");
		CHECK(buf.line_eol(0) == Eol::crlf);
		CHECK(buf.line_eol(1) == Eol::none);
		CHECK(buf.line_of(3) == 1);
	}
	SECTION("одиночный CR")
	{
		const std::string src = "a\rb\rc";
		TextBuffer buf(mem(src));
		check_matches_reference(buf, src, "CR");
		CHECK(buf.line_eol(0) == Eol::cr);
		CHECK(buf.line_count() == 3);
	}
	SECTION("смешанные переводы")
	{
		const std::string src = "a\r\nb\nc\rd\r\n\r\n";
		TextBuffer buf(mem(src));
		check_matches_reference(buf, src, "mixed");
	}
}

TEST_CASE("вставка и удаление сохраняют индекс строк", "[textbuf]")
{
	std::string model = "Первый\r\nВторой\nТретий";
	TextBuffer buf(mem(model));

	buf.insert(6, " (новый)");	// внутрь первой строки
	model.insert(6, " (новый)");
	check_matches_reference(buf, model, "вставка в строку");
	CHECK(buf.modified());

	buf.insert(0, ">>>\n");
	model.insert(0, ">>>\n");
	check_matches_reference(buf, model, "вставка перевода строки");

	buf.erase(0, 5);
	model.erase(0, 5);
	check_matches_reference(buf, model, "удаление перевода строки");

	buf.erase(model.size() - 6, 6);
	model.erase(model.size() - 6, 6);
	check_matches_reference(buf, model, "удаление в конце");

	// Склейка CRLF: удалить '\n' в конце строки с CRLF.
	const std::string crlf = "ab\r\ncd";
	TextBuffer b2(mem(crlf));
	b2.erase(3, 1); // остаётся "ab\rcd" — одиночный CR
	check_matches_reference(b2, "ab\rcd", "CRLF -> CR");
}

TEST_CASE("произвольные правки совпадают с эталоном", "[textbuf]")
{
	Rng rng;
	const std::string original = "начало\ntext line\r\nещё строка\n";
	std::string model = original;
	TextBuffer buf(mem(model));

	// Модель после каждого шага: undo_ хранит не больше 1000 групп.
	std::vector<std::string> snap{ model };
	const char* words[] = { "a", "\n", "\r\n", "\r", "xyz", "  ", "конец" };
	for (int step = 0; step < 1400; ++step)
	{
		const std::size_t where = rng.range(model.size() + 1);
		if (rng.next() % 3 != 0 || model.empty())
		{
			const char* w = words[rng.range(std::size(words))];
			buf.insert(where, w);
			model.insert(where, w);
		}
		else if (where < model.size())
		{
			const std::size_t len = 1 + rng.range(std::min<std::size_t>(8, model.size() - where));
			buf.erase(where, len);
			model.erase(where, len);
		}
		else
		{
			continue; // правка некуда: шага нет
		}
		snap.push_back(model);
		if (step % 100 == 0)
			check_matches_reference(buf, model, "шаг случайных правок");
	}
	check_matches_reference(buf, model, "итог случайных правок");

	// Отмена: каждая группа возвращает состояние до соответствующего шага
	// (кроме самых старых, которые уже вытеснены лимитом).
	std::size_t undone = 0;
	while (buf.can_undo())
	{
		REQUIRE(buf.undo());
		++undone;
		check_matches_reference(buf, snap[snap.size() - 1 - undone], "отмена");
	}
	CHECK(undone <= 1000);

	// Повтор возвращает итоговое состояние.
	std::size_t redone = 0;
	while (buf.can_redo())
	{
		REQUIRE(buf.redo());
		++redone;
		check_matches_reference(buf, snap[snap.size() - 1 - undone + redone], "повтор");
	}
	CHECK(redone == undone);
	CHECK(buf.text() == model);
}

TEST_CASE("undo/redo: группа правок — один шаг", "[textbuf]")
{
	TextBuffer buf(mem("abc"));
	buf.begin_group("печать");
	buf.insert(3, "d");
	buf.insert(4, "e");
	buf.insert(5, "f");
	buf.end_group();
	CHECK(buf.text() == "abcdef");
	CHECK(buf.undo_depth() == 1);

	REQUIRE(buf.undo());
	CHECK(buf.text() == "abc");
	CHECK(buf.line_count() == 1);
	REQUIRE(buf.redo());
	CHECK(buf.text() == "abcdef");
	CHECK(!buf.can_redo());

	// Новая правка сбрасывает redo.
	REQUIRE(buf.undo());
	buf.insert(0, ">");
	CHECK(!buf.can_redo());
	CHECK(buf.text() == ">abc");
}

TEST_CASE("правки в большом тексте не портят индекс", "[textbuf]")
{
	std::string src;
	src.reserve(1u << 20);
	for (int i = 0; i < 20000; ++i)
	{
		src += "строка ";
		src += std::to_string(i);
		src += (i % 3 == 0) ? "\r\n" : "\n";
	}
	TextBuffer buf(mem(src));
	const std::size_t lines = buf.line_count();
	CHECK(lines > 20000);

	const std::string ins = "ВСТАВКА\n";
	std::string model = src;
	const std::size_t mid = src.size() / 2;
	buf.insert(mid, ins);
	model.insert(mid, ins);
	CHECK(buf.line_count() == lines + 1);
	CHECK(std::string(buf.line_text(buf.line_of(mid))).find("ВСТАВКА") != std::string::npos);

	// Удаление внутри вставленного куска: индекс пересобирается вокруг правки.
	buf.erase(mid + ins.size() - 4, 3);
	model.erase(mid + ins.size() - 4, 3);
	check_matches_reference(buf, model, "большой текст после правок");
}

TEST_CASE("mmap: файл отображается без копирования и не меняется", "[textbuf]")
{
	const auto dir = std::filesystem::temp_directory_path() / "yo-textbuf-test";
	std::filesystem::create_directories(dir);
	const auto path = dir / "sample.txt";
	const std::string content = "Первая строка\r\nВторая строка\n";
	{
		std::FILE* f = std::fopen(path.string().c_str(), "wb");
		REQUIRE(f != nullptr);
		std::fwrite(content.data(), 1, content.size(), f);
		std::fclose(f);
	}

	std::string err;
	auto mapped = MappedFile::open(path.string(), &err);
	REQUIRE(mapped != nullptr);
	CHECK(mapped->size() == content.size());
	CHECK(mapped->view() == content);

	TextBuffer buf(mapped);
	check_matches_reference(buf, content, "mmap");
	buf.insert(0, "0\n");
	CHECK(buf.line_count() == 4);
	// Источник не изменился.
	CHECK(mapped->view() == content);

	std::filesystem::remove_all(dir);
}
