// Перенос по словам: визуальные ряды, геометрия и попадание мышью.
//
// Раскладка моноширинная: 10 px на символ, 20 px на ряд. Вьюпорт шириной
// 100 px — 10 «клеток» в ряду.

#include "yo/editor.hpp"
#include "yo/mapped.hpp"
#include "yo/textbuf.hpp"

#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <string>

using namespace yo;

namespace {

std::shared_ptr<Editor> make_editor(std::string text, bool wrap)
{
	auto ed = std::make_shared<Editor>(
		std::make_shared<TextBuffer>(std::make_shared<MemorySource>(std::move(text))));
	Metrics m;
	m.char_width = 10.0F;
	m.line_height = 20.0F;
	m.tab_cells = 4.0F;
	ed->set_metrics(m);
	ed->set_viewport(100.0F, 200.0F); // 10 клеток в ряду, 10 рядов в окне
	if (wrap)
		ed->set_word_wrap(true);
	return ed;
}

} // namespace

TEST_CASE("перенос: без переноса раскладка прежняя", "[wrap]")
{
	auto ed = make_editor("0123456789abcdefghij\nвторая\n", false);
	REQUIRE_FALSE(ed->word_wrap());
	CHECK(ed->row_count() == 3); // две строки + пустая после перевода строки
	CHECK(ed->line_top(0) == 0.0F);
	CHECK(ed->line_top(1) == 20.0F);
	CHECK(ed->content_height() == 60.0F);
	// Клик по второй строке, третий символ (кириллица — 2 байта на символ).
	CHECK(ed->offset_at(25.0F, 25.0F) == ed->buffer().line_start(1) + 6);
}

TEST_CASE("перенос: длинная строка занимает несколько рядов", "[wrap]")
{
	auto ed = make_editor("0123456789abcdefghij\nвторая\n", true);
	REQUIRE(ed->word_wrap());
	CHECK(ed->wrap_cells() == 10);
	// 20 символов при 10 клетках в ряду — два ряда, дальше вторая строка
	// («вторая», один ряд) и пустая после перевода строки.
	CHECK(ed->row_count() == 4);
	CHECK(ed->line_top(0) == 0.0F);
	CHECK(ed->line_top(1) == 40.0F); // вторая строка начинается третьим рядом
	CHECK(ed->content_height() == 80.0F);

	const std::vector<Editor::Row> rows = ed->visible_rows();
	REQUIRE(rows.size() >= 3);
	CHECK(rows[0].line == 0);
	CHECK(rows[0].from == 0);
	CHECK(rows[0].to == 10);
	CHECK(rows[0].first_of_line);
	CHECK_FALSE(rows[0].last_of_line);
	CHECK(rows[1].line == 0);
	CHECK(rows[1].from == 10);
	CHECK(rows[1].to == 20);
	CHECK_FALSE(rows[1].first_of_line);
	CHECK(rows[1].last_of_line);
	CHECK(rows[2].line == 1);
	CHECK(rows[2].top == 40.0F);
	CHECK(rows[2].from == 0);
}

TEST_CASE("перенос: по словам, а не по символам", "[wrap]")
{
	// «один два три» — 12 символов; при 10 клетках перенос после «два».
	auto ed = make_editor("один два три\n", true);
	const std::vector<Editor::Row> rows = ed->visible_rows();
	REQUIRE(rows.size() == 3); // два ряда + пустая строка после «\n»
	const std::string text = "один два три\n";
	CHECK(text.substr(rows[0].from, rows[0].to - rows[0].from) == "один два ");
	CHECK(text.substr(rows[1].from, rows[1].to - rows[1].from) == "три");
}

TEST_CASE("перенос: слово без пробелов режется по границе ряда", "[wrap]")
{
	auto ed = make_editor("0123456789abc\n", true);
	const std::vector<Editor::Row> rows = ed->visible_rows();
	REQUIRE(rows.size() == 3);
	CHECK(rows[0].to == 10);
	CHECK(rows[1].from == 10);
	CHECK(rows[1].to == 13);
}

TEST_CASE("перенос: клик попадает в символ на любом ряду", "[wrap]")
{
	auto ed = make_editor("0123456789abcdefghij", true);
	const std::size_t second_row_start = 10;
	// Второй ряд — третий символ («c»), x = 25 px, y = 25 px (второй ряд).
	CHECK(ed->offset_at(25.0F, 25.0F) == second_row_start + 3);
	// Левый край второго ряда — его начало.
	CHECK(ed->offset_at(0.0F, 30.0F) == second_row_start);
	// Первый ряд — как обычно.
	CHECK(ed->offset_at(35.0F, 5.0F) == 4);
	// Ниже последнего ряда — его начало (клик по пустому месту).
	CHECK(ed->offset_at(0.0F, 500.0F) == second_row_start);
}

TEST_CASE("перенос: курсор идёт по рядам, столбец сохраняется", "[wrap]")
{
	auto ed = make_editor("0123456789abcdefghij\nкороткая\n", true);
	ed->set_caret(0);
	CHECK(ed->caret_y() == 0.0F);
	ed->move_down();
	// Тот же столбец, но уже второй ряд первой строки.
	CHECK(ed->caret() == 10);
	CHECK(ed->caret_y() == 20.0F);
	CHECK(ed->caret_x() == 0.0F);
	ed->move_down();
	CHECK(ed->caret_y() == 40.0F); // вторая логическая строка
	CHECK(ed->buffer().line_of(ed->caret()) == 1);
	ed->move_up();
	CHECK(ed->caret_y() == 20.0F);
}

TEST_CASE("перенос: Home/End и столбец в статусе остаются логическими", "[wrap]")
{
	auto ed = make_editor("0123456789abcdefghij\n", true);
	ed->move_line_end();
	CHECK(ed->caret() == 20);
	CHECK(ed->caret_pos().column == 21); // столбец в символах строки
	ed->move_line_start();
	CHECK(ed->caret() == 0);
}

TEST_CASE("перенос: прокрутка считается в рядах", "[wrap]")
{
	auto ed = make_editor("0123456789abcdefghij\n", true); // 2 ряда + пустая строка
	CHECK(ed->content_height() == 60.0F);
	ed->scroll_by(0.0F, 1000.0F); // больше некуда
	CHECK(ed->scroll_y() == std::max(0.0F, ed->content_height() - ed->viewport_height()));
	CHECK(ed->scroll_x() == 0.0F); // горизонтальной прокрутки при переносе нет
}

TEST_CASE("перенос: очень длинная строка не переносится", "[wrap]")
{
	// 30000 символов без пробелов — выше предела раскладки: одна строка.
	std::string text(30000, 'x');
	text += "\n";
	auto ed = make_editor(text, true);
	CHECK(ed->row_count() == 2); // строка не переносится + пустая после «\n»
	CHECK(ed->content_height() == 40.0F);
}

TEST_CASE("перенос: правка сбрасывает раскладку рядов", "[wrap]")
{
	auto ed = make_editor("0123456789abcdefghij\n", true);
	CHECK(ed->row_count() == 3); // два ряда + пустая строка
	ed->set_caret(0);
	ed->insert_text("12345"); // строка стала длиннее — рядов больше
	CHECK(ed->row_count() == 4);
	CHECK(ed->line_top(1) == 60.0F); // вторая строка сдвинулась вниз
}

TEST_CASE("перенос: полоса прокрутки меряется рядами", "[wrap]")
{
	// 30 строк по 20 символов: при 10 клетках в ряду — по 2 ряда на строку.
	std::string text;
	for (int i = 0; i < 30; ++i)
		text += "0123456789abcdefghij\n";
	auto ed = make_editor(text, true);
	REQUIRE(ed->word_wrap());
	CHECK(ed->row_count() == 61); // 30 строк * 2 ряда + пустая после «\n»
	CHECK(ed->visible_row_count() == 11); // 10 рядов в окне (+1 запас)
	ed->scroll_by(0.0F, 20.0F * 4.0F);
	CHECK(ed->first_visible_row() == 4);
	CHECK(ed->first_visible_line() == 2);
}

TEST_CASE("перенос: табуляция считается до границы остановки", "[wrap]")
{
	// «a» + таб (до 4) + «bcd» = 8 клеток, далее «eee…» — перенос.
	auto ed = make_editor("a\tbcd0123456789\n", true);
	const std::vector<Editor::Row> rows = ed->visible_rows();
	REQUIRE(rows.size() >= 2);
	// Первый ряд: «a», таб (3 клетки), «bcd» (итого 6), «012» — 10 клеток.
	const std::string text = "a\tbcd0123456789\n";
	CHECK(text.substr(rows[0].from, rows[0].to - rows[0].from) == "a\tbcd012");
	CHECK(ed->row_cells(rows[0], rows[0].to) == 10);
}

TEST_CASE("перенос: переключение режима не теряет курсор", "[wrap]")
{
	auto ed = make_editor("0123456789abcdefghij\nвторая\n", false);
	ed->set_caret(12); // второй ряд первой строки
	CHECK(ed->caret_y() == 0.0F);
	ed->set_word_wrap(true);
	CHECK(ed->caret() == 12);
	CHECK(ed->caret_y() == 20.0F);
	CHECK(ed->caret_x() == 20.0F); // 2 символа от начала ряда
	ed->set_word_wrap(false);
	CHECK(ed->caret() == 12);
	CHECK(ed->caret_y() == 0.0F);
	CHECK(ed->caret_x() == 120.0F);
}
