// Список последних файлов: разбор строки ini и поведение списка (FIFO).

#include "yo/mru.hpp"

#include <catch2/catch_test_macros.hpp>

#include <string>

using namespace yo;

TEST_CASE("MRU: разбор строки Files.NameN", "[mru]")
{
	const MruEntry plain = mru_parse("C:\\texts\\роман.txt");
	CHECK(plain.path == "C:\\texts\\роман.txt");
	CHECK(plain.position == 0);
	CHECK(plain.flags.empty());

	const MruEntry full = mru_parse("C:\\texts\\роман.txt,12345,uS");
	CHECK(full.path == "C:\\texts\\роман.txt");
	CHECK(full.position == 12345);
	CHECK(full.flags == "uS");
	CHECK(full.utf8());
	CHECK(full.bom());
	CHECK_FALSE(full.utf16());

	// Флагов нет, а хвост — число: это позиция, не флаги.
	const MruEntry pos_only = mru_parse("роман.txt,42");
	CHECK(pos_only.path == "роман.txt");
	CHECK(pos_only.position == 42);
	CHECK(pos_only.flags.empty());

	// Мусор в позиции читается как 0, путь не теряется.
	const MruEntry junk = mru_parse("роман.txt,12px,u");
	CHECK(junk.path == "роман.txt");
	CHECK(junk.position == 0);
	CHECK(junk.flags == "u");

	const MruEntry rtf = mru_parse("a.rtf,7,R");
	CHECK(rtf.rtf());
}

TEST_CASE("MRU: запись строки и повторное чтение", "[mru]")
{
	MruEntry e;
	e.path = "C:\\книги\\повесть.fb2";
	e.position = 2048;
	e.flags = "uS";
	const std::string text = mru_format(e);
	CHECK(text == "C:\\книги\\повесть.fb2,2048,uS");

	const MruEntry back = mru_parse(text);
	CHECK(back.path == e.path);
	CHECK(back.position == e.position);
	CHECK(back.flags == e.flags);
}

TEST_CASE("MRU: список FIFO и предел в девять файлов", "[mru]")
{
	MruList list;
	for (int i = 0; i < 12; ++i)
		list.add(MruEntry{ "файл" + std::to_string(i) + ".txt", 0, "" });

	REQUIRE(list.size() == kMruMax);
	CHECK(list.entries().front().path == "файл3.txt"); // первые три вытеснены
	CHECK(list.entries().back().path == "файл11.txt");

	// Повторное открытие порядок не меняет (FIFO, не LRU), а запомненная
	// позиция курсора не затирается: для неё отдельный remember().
	const std::size_t at = list.add(MruEntry{ "файл5.txt", 0, "" });
	CHECK(at == 2);
	CHECK(list.entries()[2].position == 0);
	list.remember("файл5.txt", 77, "u");
	CHECK(list.entries()[2].position == 77);
	CHECK(list.entries()[2].flags == "u");
	CHECK(list.size() == kMruMax);
}

TEST_CASE("MRU: поиск пути без учёта регистра и удаление", "[mru]")
{
	MruList list;
	list.add(MruEntry{ "C:\\Тексты\\Роман.TXT", 10, "" });
	CHECK(list.find("c:\\тексты\\роман.txt") == 0);
	CHECK(list.find("C:\\другие\\роман.txt") == kMruMax);

	list.remember("C:\\тексты\\роман.txt", 500, "uS");
	REQUIRE(list.at(0) != nullptr);
	CHECK(list.at(0)->position == 500);
	CHECK(list.at(0)->flags == "uS");

	list.remove("C:\\ТЕКСТЫ\\РОМАН.TXT");
	CHECK(list.empty());
	CHECK(list.at(0) == nullptr);
}

TEST_CASE("MRU: загрузка из настроек без дубликатов", "[mru]")
{
	std::vector<MruEntry> entries{
		MruEntry{ "a.txt", 1, "" },
		MruEntry{ "b.txt", 2, "" },
		MruEntry{ "A.TXT", 3, "" }, // дубликат по регистру
		MruEntry{ "", 4, "" },		// пустой путь — пропускаем
	};
	MruList list;
	list.set(entries);
	REQUIRE(list.size() == 2);
	CHECK(list.entries()[0].path == "a.txt");
	CHECK(list.entries()[0].position == 1); // первый из дубликатов
	CHECK(list.entries()[1].path == "b.txt");
}
