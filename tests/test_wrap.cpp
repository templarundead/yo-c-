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

TEST_CASE("перенос: видимые ряды без переноса (регресс падения отрисовки)", "[wrap]")
{
	// Без переноса visible_rows() обязан работать так же, как список строк:
	// раньше здесь читалась пустая таблица рядов, и отрисовка падала
	// (белое окно) на первом же кадре.
	auto ed = make_editor("первая\nвторая\nтретья\n", false);
	REQUIRE_FALSE(ed->word_wrap());
	const std::vector<Editor::Row> rows = ed->visible_rows();
	REQUIRE(rows.size() == 4); // три строки + пустая после последнего «\n»
	CHECK(rows[0].line == 0);
	CHECK(rows[1].line == 1);
	CHECK(rows[2].line == 2);
	CHECK(rows[3].line == 3);
	CHECK(rows[3].to == 0);
	CHECK(rows[1].from == 0);
	CHECK(rows[1].to == 12); // «вторая» — 6 символов по 2 байта
	CHECK(rows[1].top == 20.0F);
	CHECK(rows[1].first_of_line);
	CHECK(rows[1].last_of_line);

	// То, что делает рендерер на каждом кадре: текст строки и ширина префиксов.
	for (const Editor::Row& row : rows)
	{
		const std::string_view text = ed->buffer().line_text(row.line);
		CHECK(row.to <= text.size());
		CHECK(ed->row_width(row, row.from) == 0.0F);
		CHECK(ed->row_width(row, row.to) >= 0.0F);
	}

	// Прокрутка вниз (окно — два ряда): первый ряд — третья строка.
	ed->set_viewport(100.0F, 40.0F);
	ed->scroll_by(0.0F, 40.0F);
	const std::vector<Editor::Row> scrolled = ed->visible_rows();
	REQUIRE_FALSE(scrolled.empty());
	CHECK(scrolled.front().line == 2);
	CHECK(ed->first_visible_row() == 2);
	CHECK(ed->visible_row_count() == 2); // третья строка и пустая
}

TEST_CASE("перенос: видимые ряды — пустой буфер и один ряд", "[wrap]")
{
	auto ed = make_editor("", false);
	const std::vector<Editor::Row> rows = ed->visible_rows();
	REQUIRE(rows.size() == 1); // одна пустая строка
	CHECK(rows[0].line == 0);
	CHECK(rows[0].from == 0);
	CHECK(rows[0].to == 0);

	ed->set_word_wrap(true);
	const std::vector<Editor::Row> wrapped = ed->visible_rows();
	REQUIRE(wrapped.size() == 1);
	CHECK(wrapped[0].line == 0);
}

TEST_CASE("перенос: рендерерный обход рядов в обоих режимах", "[wrap]")
{
	// Полный проход «как в paint»: текст, выделение и подсветка по каждому
	// видимому ряду — ловит выход за границы строки и нулевые срезы.
	const std::string text = "0123456789abcdefghij\nвторая строка подлиннее\n\nтретья\n";
	for (const bool wrap : { false, true })
	{
		auto ed = make_editor(text, wrap);
		ed->set_viewport(100.0F, 60.0F);
		ed->scroll_by(0.0F, 20.0F);
		const std::vector<Editor::Row> rows = ed->visible_rows();
		REQUIRE_FALSE(rows.empty());
		for (const Editor::Row& row : rows)
		{
			const std::string_view bytes = ed->buffer().line_text(row.line);
			REQUIRE(row.from <= row.to);
			REQUIRE(row.to <= bytes.size());
			CHECK(ed->row_width(row, row.from) == 0.0F);
			CHECK(ed->row_width(row, row.to) >= 0.0F);
			CHECK(ed->row_of_offset(ed->buffer().line_start(row.line) + row.from) <=
				  ed->row_count());
		}
	}
}

TEST_CASE("перенос: стресс геометрии в обоих режимах", "[wrap]")
{
	// Детерминированный обход: двигаем курсор и прокрутку и запрашиваем всю
	// геометрию, которой пользуется рендерер. Ловит выходы за границы после
	// правок в раскладке (под ASan/UBSan).
	std::string text;
	text += "короткая строка\n";
	text += std::string(120, 'x') + "\n";
	text += "с табуляцией\tи пробелами здесь\n";
	text += "\n";
	text += "ёжик ёлка тёмный\n";
	text += std::string(40, 'y');

	for (const bool wrap : { false, true })
	{
		auto ed = make_editor(text, wrap);
		for (int step = 0; step < 60; ++step)
		{
			switch (step % 6)
			{
			case 0:
				ed->move_right();
				break;
			case 1:
				ed->move_down();
				break;
			case 2:
				ed->move_up();
				break;
			case 3:
				ed->scroll_by(0.0F, 25.0F);
				break;
			case 4:
				ed->move_doc_end();
				break;
			default:
				ed->move_doc_start();
				ed->move_line_end();
				break;
			}
			const std::vector<Editor::Row> rows = ed->visible_rows();
			for (const Editor::Row& row : rows)
			{
				const std::string_view bytes = ed->buffer().line_text(row.line);
				REQUIRE(row.from <= row.to);
				REQUIRE(row.to <= bytes.size());
				CHECK(ed->row_width(row, row.from) == 0.0F);
				CHECK(ed->row_width(row, bytes.size()) >= 0.0F);
			}
			// Точки на сетке экрана должны попадать в допустимые смещения.
			for (float x : { -5.0F, 0.0F, 37.0F, 250.0F })
			{
				for (float y : { -5.0F, 0.0F, 45.0F, 400.0F })
				{
					const std::size_t off = ed->offset_at(x, y);
					CHECK(off <= ed->buffer().size());
				}
			}
			CHECK(ed->row_of_offset(ed->caret()) < std::max<std::size_t>(1, ed->row_count()));
			CHECK(ed->caret_x() >= 0.0F);
			CHECK(ed->caret_y() >= 0.0F);
		}
	}
}

TEST_CASE("прокрутка к ряду: слово остаётся над панелью вопросов", "[wrap]")
{
	// Строка из 30 символов при 10 клетках в ряду — три ряда, затем ещё
	// 20 коротких строк. Вьюпорт в 10 рядов, но под панель вопросов оставляем
	// только 6 рядов: видимая область стала меньше, и прокрутка «по строкам»
	// (прежнее поведение) оставляла слово за панелью.
	auto ed = make_editor("012345678901234567890123456789\n"
						  "a\nb\nc\nd\ne\nf\ng\nh\ni\nj\nk\nl\nm\nn\no\np\nq\nr\ns\n",
						  true);
	REQUIRE(ed->word_wrap());
	ed->set_viewport(100.0F, 120.0F); // шесть рядов в окне — над панелью
	const std::size_t rows_total = ed->row_count();
	REQUIRE(rows_total > 20);

	// Ряд в конце строки-«простыни» — это 2-й или 3-й ряд одной строки.
	const std::size_t last_of_long = ed->row_of_offset(29);
	REQUIRE(last_of_long > 0);

	long delta = ed->scroll_delta_to_reveal_row(last_of_long, true, 0);
	REQUIRE(delta != 0);
	ed->scroll_lines(delta);
	CHECK(ed->first_visible_row() + ed->visible_row_count() > last_of_long);
	// Строка слова целиком видна в рядах, а не «за краем».
	const std::vector<Editor::Row> rows = ed->visible_rows();
	bool seen = false;
	for (const Editor::Row& row : rows)
		seen = seen || (row.line == 0 && row.from <= 29 && row.to > 29);
	CHECK(seen);

	// С запасом строк промотки слово поднимается выше: снизу остаётся
	// не меньше `margin` рядов (у границы документа прокрутка упирается,
	// поэтому проверяем только середину текста).
	const long delta2 = ed->scroll_delta_to_reveal_row(last_of_long, true, 2);
	ed->scroll_lines(delta2);
	const std::size_t first2 = ed->first_visible_row();
	const std::size_t count2 = ed->visible_row_count();
	REQUIRE(first2 <= last_of_long);
	CHECK(last_of_long < first2 + count2);
	if (first2 > 0 && first2 + count2 < rows_total)
		CHECK(last_of_long + 3 <= first2 + count2);
}

TEST_CASE("прокрутка к ряду: без переноса ряд равен строке", "[wrap]")
{
	auto ed = make_editor("aaa\nbbb\nccc\nddd\neee\nfff\n", false);
	ed->set_viewport(100.0F, 40.0F);
	const std::size_t count = ed->visible_row_count();
	REQUIRE(count >= 2);
	CHECK(ed->first_visible_row() == 0);
	// Ряды в окне — прокрутки нет, в том числе у последнего видимого.
	CHECK(ed->scroll_delta_to_reveal_row(1, false, 0) == 0);
	CHECK(ed->scroll_delta_to_reveal_row(count - 1, false, 0) == 0);
	// Первый ряд ниже окна — прокручиваем ровно на один ряд.
	CHECK(ed->scroll_delta_to_reveal_row(count, false, 0) == 1);
	ed->scroll_lines(1);
	CHECK(ed->first_visible_row() == 1);
	CHECK(ed->scroll_delta_to_reveal_row(count, false, 0) == 0);
	// Выше окна — обратно вверх.
	CHECK(ed->scroll_delta_to_reveal_row(0, false, 0) == -1);
	// at_bottom тянет ряд к нижнему краю; у начала документа прокрутка
	// упирается в ноль, и ряд остаётся первым видимым.
	ed->scroll_lines(ed->scroll_delta_to_reveal_row(0, true, 0));
	CHECK(ed->first_visible_row() == 0);
	// Дальше от границы ряд действительно встаёт последним видимым.
	ed->scroll_lines(ed->scroll_delta_to_reveal_row(count + 1, true, 0));
	REQUIRE(ed->first_visible_row() > 0);
	CHECK(ed->first_visible_row() + ed->visible_row_count() - 1 == count + 1);
	CHECK(ed->scroll_delta_to_reveal_row(count + 1, true, 0) == 0);
}
