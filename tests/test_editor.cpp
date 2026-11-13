// Редактор: курсор, выделение, правки, прокрутка и геометрия строк
// (моноширинная раскладка). Тесты не зависят от Win32.

#include "yo/editor.hpp"
#include "yo/mapped.hpp"
#include "yo/textbuf.hpp"

#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <string>

using namespace yo;

namespace {

std::shared_ptr<TextBuffer> tb(std::string text)
{
	return std::make_shared<TextBuffer>(std::make_shared<MemorySource>(std::move(text)));
}

std::shared_ptr<Editor> editor(std::string text)
{
	return std::make_shared<Editor>(tb(std::move(text)));
}

// «абв» в UTF-8: каждый символ — 2 байта.
const std::string kCyr3 = "абв";

} // namespace

TEST_CASE("курсор: горизонтальное движение по UTF-8", "[editor]")
{
	auto ed = editor(kCyr3); // 6 байт
	ed->move_right();
	CHECK(ed->caret() == 2);
	ed->move_right();
	CHECK(ed->caret() == 4);
	ed->move_right();
	CHECK(ed->caret() == 6);
	ed->move_right();
	CHECK(ed->caret() == 6); // в конце не уезжает
	ed->move_left();
	CHECK(ed->caret() == 4);
	ed->move_left();
	ed->move_left();
	CHECK(ed->caret() == 0);
	ed->move_left();
	CHECK(ed->caret() == 0);
}

TEST_CASE("курсор: вертикальное движение сохраняет колонку", "[editor]")
{
	auto ed = editor("abcdef\nxyz\n0123456789");
	ed->set_caret(5); // 'f' в первой строке, колонка 6 (1-based)
	ed->move_down();
	CHECK(ed->caret() == 7 + 3); // вторая строка короче — в её конец
	ed->move_down();
	CHECK(ed->caret() == 11 + 5); // колонка восстановлена (5 символов от начала)
	ed->move_up();
	CHECK(ed->caret() == 7 + 3);
	ed->move_up();
	CHECK(ed->caret() == 5); // желаемая колонка помнится между строками
}

TEST_CASE("курсор: дом/конец строки, документа, страницы", "[editor]")
{
	auto ed = editor("aaa\nbbb\nccc");
	ed->set_viewport(800.0F, 17.0F * 2.0F); // две строки в окне
	ed->set_caret(5);
	ed->move_line_start();
	CHECK(ed->caret() == 4);
	ed->move_line_end();
	CHECK(ed->caret() == 7);
	ed->move_doc_start();
	CHECK(ed->caret() == 0);
	ed->move_doc_end();
	CHECK(ed->caret() == 11);
	ed->move_doc_start();
	ed->move_page(1);
	CHECK(ed->caret() == 8); // две строки вниз: начало «ccc»
	ed->move_page(-1);
	CHECK(ed->caret() == 0);
}

TEST_CASE("курсор: слова", "[editor]")
{
	auto ed = editor("раз два  три");
	ed->move_doc_start();
	ed->move_right(false, true); // к концу «раз» (3 символа по 2 байта)
	CHECK(ed->caret() == 6);
	ed->move_right(false, true); // через пробел к концу «два»
	CHECK(ed->caret() == 13);
	ed->move_left(false, true); // к началу «два»
	CHECK(ed->caret() == 7);
	ed->move_left(false, true);
	CHECK(ed->caret() == 0);
}

TEST_CASE("правки: ввод, замена выделения, backspace/delete", "[editor]")
{
	auto ed = editor("абв");
	ed->set_caret(2);
	ed->insert_text("г"); // «агбв»
	CHECK(ed->buffer().text() == "агбв");
	CHECK(ed->caret() == 4);

	// Выделение и замена.
	ed->set_caret(0);
	ed->set_caret(4, true); // выделены «аг»
	CHECK(ed->selected_text() == "аг");
	ed->insert_text("д");
	CHECK(ed->buffer().text() == "дбв");
	CHECK(ed->caret() == 2);
	CHECK(!ed->has_selection());

	// Backspace склеивает строки: «дб» = 4 байта, перевод строки — после них.
	ed->buffer().insert(4, "\n");
	CHECK(ed->buffer().line_count() == 2);
	ed->set_caret(5); // начало второй строки: «дб» = 4 байта + '\n'
	ed->backspace();
	CHECK(ed->buffer().text() == "дбв");
	CHECK(ed->buffer().line_count() == 1);

	// Delete вперёд удаляет символ целиком (UTF-8).
	ed->set_caret(0);
	ed->delete_forward();
	CHECK(ed->buffer().text() == "бв");
	ed->set_caret(4);     // конец текста «бв» (4 байта)
	ed->delete_forward(); // в конце — ничего
	CHECK(ed->buffer().text() == "бв");
}

TEST_CASE("правки: вырезать/копировать/вставить", "[editor]")
{
	auto ed = editor("один два три");
	ed->set_caret(9, false);  // «один» = 8 байт, дальше пробел
	ed->set_caret(15, true); // «два» = 6 байт
	CHECK(ed->selected_text() == "два");

	std::string buf;
	CHECK(ed->copy_selection(&buf));
	CHECK(buf == "два");
	CHECK(ed->buffer().text() == "один два три"); // копирование не меняет текст

	CHECK(ed->cut_selection(&buf));
	CHECK(buf == "два");
	CHECK(ed->buffer().text() == "один  три");
	CHECK(ed->caret() == 9);

	ed->move_doc_end();
	ed->paste("!");
	CHECK(ed->buffer().text() == "один  три!");
	ed->paste(buf);
	CHECK(ed->buffer().text() == "один  три!два");
}

TEST_CASE("статусбар: строка и столбец", "[editor]")
{
	auto ed = editor("abc\n\tде\nxyz");
	ed->set_caret(0);
	CHECK(ed->caret_pos().line == 1);
	CHECK(ed->caret_pos().column == 1);
	ed->set_caret(2);
	CHECK(ed->caret_pos().column == 3);

	// Табуляция = 4 клетки, кириллица — по одной клетке на символ.
	ed->set_caret(5); // сразу после '\t'
	CHECK(ed->caret_pos().line == 2);
	CHECK(ed->caret_pos().column == 5);
	ed->set_caret(7); // после «д»
	CHECK(ed->caret_pos().column == 6);
}

TEST_CASE("геометрия: пиксели и смещения согласованы", "[editor]")
{
	auto ed = editor("ab\r\n\tвг\nxyz");
	Metrics m;
	m.line_height = 20.0F;
	m.char_width = 10.0F;
	m.tab_cells = 4.0F;
	ed->set_metrics(m);
	ed->set_viewport(800.0F, 200.0F);

	// Строка 0 — «ab»: x растёт по 10 px.
	ed->set_caret(1);
	CHECK(ed->caret_x() == 10.0F);
	CHECK(ed->caret_y() == 0.0F);

	// Строка 1 — «\tвг»: табуляция до 4 клеток (строка занимает байты 4..9).
	ed->set_caret(9); // конец строки 1
	CHECK(ed->caret_y() == 20.0F);
	CHECK(ed->caret_x() == 60.0F); // 4 клетки табуляции + 2 символа

	// Клик в пикселях возвращает то же смещение.
	CHECK(ed->offset_at(10.0F, 0.0F) == 1);
	CHECK(ed->offset_at(11.0F, 0.0F) == 1);
	CHECK(ed->offset_at(15.0F, 0.0F) == 2);
	CHECK(ed->offset_at(60.0F, 20.0F) == 9);
	CHECK(ed->offset_at(42.0F, 20.0F) == 5); // первая половина «в» — курсор перед ней
	CHECK(ed->offset_at(45.0F, 20.0F) == 7); // вторая половина — за ней
	CHECK(ed->offset_at(0.0F, 20.0F) == 4);
	// За пределами текста — крайние позиции.
	CHECK(ed->offset_at(-50.0F, -50.0F) == 0);
	CHECK(ed->offset_at(9999.0F, 9999.0F) == ed->buffer().size());
}

TEST_CASE("прокрутка: курсор всегда в окне", "[editor]")
{
	auto ed = editor("1\n2\n3\n4\n5\n6\n7\n8\n9\n10");
	Metrics m;
	m.line_height = 10.0F;
	m.char_width = 8.0F;
	ed->set_metrics(m);
	ed->set_viewport(100.0F, 30.0F); // 3 строки

	CHECK(ed->content_height() == 100.0F);
	CHECK(ed->first_visible_line() == 0);

	ed->move_doc_end();
	CHECK(ed->scroll_y() == 70.0F); // 100 - 30
	CHECK(ed->first_visible_line() == 7);

	ed->move_doc_start();
	CHECK(ed->scroll_y() == 0.0F);

	// Прокрутка колесом и её пределы.
	ed->scroll_lines(2);
	CHECK(ed->scroll_y() == 20.0F);
	ed->scroll_lines(-100);
	CHECK(ed->scroll_y() == 0.0F);
	ed->scroll_lines(1000);
	CHECK(ed->scroll_y() == 70.0F);

	// Горизонтальная прокрутка длинной строки.
	auto wide = editor(std::string(100, 'x'));
	wide->set_metrics(m);
	wide->set_viewport(100.0F, 30.0F);
	CHECK(wide->text_width() == 800.0F);
	wide->move_doc_end();
	CHECK(wide->scroll_x() == 800.0F - 100.0F + 8.0F);
	CHECK(wide->visible_line_count() == 1);
}

TEST_CASE("видимые строки: ленивый диапазон", "[editor]")
{
	std::string text;
	for (int i = 0; i < 1000; ++i)
	{
		text += "строка ";
		text += std::to_string(i);
		text += "\n";
	}
	auto ed = editor(text);
	Metrics m;
	m.line_height = 16.0F;
	ed->set_metrics(m);
	ed->set_viewport(600.0F, 160.0F); // 10 строк

	CHECK(ed->first_visible_line() == 0);
	CHECK(ed->visible_line_count() == 11); // 10 + 1 запас

	ed->scroll_lines(50);
	CHECK(ed->first_visible_line() == 50);
	CHECK(ed->visible_line_count() == 11);
	CHECK(std::string(ed->buffer().line_text(ed->first_visible_line())) == "строка 50");

	ed->scroll_lines(10000);
	CHECK(ed->first_visible_line() == 991); // упор в низ: 1001 строка, окно 10
	CHECK(ed->visible_line_count() == 10);

	// Правка в начале не ломает диапазон.
	ed->scroll_lines(-10000);
	ed->set_caret(0);
	ed->insert_text("начало\n");
	CHECK(ed->buffer().line_count() == 1002);
	CHECK(ed->first_visible_line() == 0);
}

TEST_CASE("видимость ряда: координаты, а не высота окна", "[editor]")
{
	// Полоса окна [26, 230] — как в рендерере: 26 px полоса меню, дальше текст.
	constexpr float top = 26.0F;
	constexpr float bottom = 230.0F;
	constexpr float lh = 17.0F;

	CHECK(row_in_view(top, lh, top, bottom));
	// Последняя строка в конце документа: 213..230 — целиком в окне.
	CHECK(row_in_view(213.0F, lh, top, bottom));
	CHECK(row_in_view(bottom, lh, top, bottom));		 // начинается на границе
	CHECK(row_in_view(top - lh, lh, top, bottom));		 // уходит под полосу меню
	CHECK(row_in_view(bottom - 1.0F, lh, top, bottom));	 // подрезан снизу

	CHECK_FALSE(row_in_view(bottom + 1.0F, lh, top, bottom));
	CHECK_FALSE(row_in_view(top - lh - 1.0F, lh, top, bottom));
}

TEST_CASE("последняя строка в конце документа видна", "[editor]")
{
	// Регрессия: проход отрисовки текста сравнивал y ряда с высотой текстовой
	// области вместо её нижней границы, поэтому последняя строка в конце
	// документа (она целиком лежит в окне) не рисовалась. Выделение рисуется
	// другим проходом, без такой проверки и до клипа, — строка выглядела пустой
	// полосой, но выделялась и копировалась.
	std::string text;
	for (int i = 0; i < 588; ++i)
	{
		text += "строка ";
		text += std::to_string(i + 1);
		text += "\n";
	}
	text += "последняя строка";
	auto ed = editor(text);

	Metrics m;
	m.line_height = 17.0F;
	m.char_width = 8.0F;
	ed->set_metrics(m);

	// Полоса меню сверху и статусбар снизу — как у настоящего окна.
	constexpr float bar = 26.0F;
	constexpr float status = 20.0F;
	for (const float height : { 200.0F, 250.0F, 400.0F, 600.0F })
	{
		const float text_h = height - bar - status;
		ed->set_viewport(1200.0F, text_h);
		ed->move_doc_end();
		REQUIRE(ed->scroll_y() > 0.0F); // документ выше окна: вид в конце

		const float area_bottom = bar + text_h;
		bool last_drawn = false;
		for (const Editor::Row& row : ed->visible_rows())
		{
			const float y = bar + row.top - ed->scroll_y();
			const bool visible = row_in_view(y, m.line_height, bar, area_bottom);
			if (!visible)
				continue;
			if (row.line + 1 != ed->buffer().line_count())
				continue;
			last_drawn = true;
			// Ряд не «подрезан краем»: он весь внутри видимой области.
			CHECK(y >= bar);
			CHECK(y + m.line_height <= area_bottom);
		}
		CHECK(last_drawn);

		// Та самая арифметика, из-за которой строка пропадала: координата
		// меньше нижней границы области, но больше её высоты.
		const float y_last =
			bar + ed->line_top(ed->buffer().line_count() - 1) - ed->scroll_y();
		CHECK(y_last > text_h);
		CHECK(y_last < area_bottom);
	}
}

TEST_CASE("редактор поверх mmap: правки не трогают файл", "[editor]")
{
	auto src = std::make_shared<MemorySource>(std::string("файл\r\nне меняется\n"));
	auto ed = std::make_shared<Editor>(std::make_shared<TextBuffer>(src));
	ed->move_doc_end();
	ed->insert_text("дописано");
	CHECK(ed->buffer().text() == "файл\r\nне меняется\nдописано");
	CHECK(src->view() == "файл\r\nне меняется\n");
	CHECK(ed->buffer().line_count() == 3);
	ed->buffer().undo();
	CHECK(ed->buffer().text() == "файл\r\nне меняется\n");
}

TEST_CASE("прокрутка: перерисовка не отматывает вид назад", "[editor]")
{
	auto ed = editor("1\n2\n3\n4\n5\n6\n7\n8\n9\n10");
	Metrics m;
	m.line_height = 10.0F;
	m.char_width = 8.0F;
	ed->set_metrics(m);
	ed->set_viewport(100.0F, 30.0F); // 3 строки в окне

	// Курсор в начале документа, пользователь прокрутил колесом/полосой.
	ed->scroll_lines(4);
	CHECK(ed->scroll_y() == 40.0F);
	CHECK(ed->first_visible_line() == 4);

	// Рендерер на каждой перерисовке заново сообщает размеры окна и метрики:
	// это не должно отменять прокрутку (регрессия «не работает скролл»).
	ed->set_viewport(100.0F, 30.0F);
	CHECK(ed->scroll_y() == 40.0F);
	CHECK(ed->first_visible_line() == 4);
	ed->set_metrics(m);
	CHECK(ed->scroll_y() == 40.0F);
	CHECK(ed->first_visible_line() == 4);

	// Горизонтальная прокрутка так же переживает перерисовку.
	auto wide = editor(std::string(200, 'x'));
	wide->set_metrics(m);
	wide->set_viewport(100.0F, 30.0F);
	wide->scroll_by(120.0F, 0.0F);
	CHECK(wide->scroll_x() == 120.0F);
	wide->set_viewport(100.0F, 30.0F);
	CHECK(wide->scroll_x() == 120.0F);

	// А вот курсор, уехавший за край, по-прежнему возвращается в поле зрения.
	ed->move_doc_start();
	CHECK(ed->scroll_y() == 0.0F);
}

TEST_CASE("клик: экранные координаты при прокрутке", "[editor]")
{
	auto ed = editor("1\n2\n3\n4\n5\n6\n7\n8\n9\n10");
	Metrics m;
	m.line_height = 10.0F;
	m.char_width = 8.0F;
	ed->set_metrics(m);
	ed->set_viewport(100.0F, 30.0F); // три строки в окне

	ed->scroll_lines(5);
	REQUIRE(ed->first_visible_line() == 5);

	// Верхняя строка окна — это строка 5 документа, а не 0 и не 10.
	CHECK(ed->offset_at_client(0.0F, 1.0F) == ed->buffer().line_start(5));
	CHECK(ed->offset_at_client(0.0F, 21.0F) == ed->buffer().line_start(7));
	// Отступ слева учитывается отдельно.
	CHECK(ed->offset_at_client(16.0F, 1.0F, 16.0F) == ed->buffer().line_start(5));
	CHECK(ed->offset_at_client(24.0F, 1.0F, 16.0F) == ed->buffer().line_start(5) + 1);

	// Без прокрутки — как раньше.
	ed->scroll_lines(-100);
	CHECK(ed->offset_at_client(0.0F, 1.0F) == 0);
	CHECK(ed->offset_at_client(0.0F, 11.0F) == ed->buffer().line_start(1));
}

TEST_CASE("вставка большого текста и прокрутка (без падений)", "[editor]")
{
	// Регрессия: вставка длинного текста роняла программу — раскладки рядов
	// в кэше рендерера перевыделялись, а указатели на них (теперь индексы)
	// оставались висячими. Здесь проверяем сам редактор: вставка 2 МБ,
	// прокрутка по всему документу и чтение геометрии видимых рядов.
	auto ed = editor("начало");
	ed->set_viewport(800.0F, 300.0F);
	std::string chunk;
	chunk.reserve(2U * 1024U * 1024U);
	for (int i = 0; i < 20000; ++i)
		chunk += "строка номер " + std::to_string(i) + " с текстом и словами\n";
	ed->move_doc_end();
	REQUIRE(ed->paste(chunk));
	CHECK(ed->buffer().line_count() > 20000U);
	// Прокрутка до конца и обратно: видимые ряды должны быть корректны.
	ed->move_doc_end();
	for (int i = 0; i < 50; ++i)
	{
		const std::vector<Editor::Row> rows = ed->visible_rows();
		REQUIRE(!rows.empty());
		for (const Editor::Row& row : rows)
		{
			CHECK(row.line < ed->buffer().line_count());
			CHECK(row.from <= row.to);
			CHECK(row.to <= ed->buffer().line_text(row.line).size());
		}
		ed->scroll_lines(-3);
	}
}

TEST_CASE("пустой документ: нет ширины текста и прокрутки", "[editor]")
{
	// Из этого считается видимость полос прокрутки: в пустом окне не должно
	// быть ни горизонтальной полосы, ни запаса по высоте.
	auto ed = editor("");
	ed->set_viewport(800.0F, 300.0F);
	CHECK(ed->buffer().size() == 0);
	CHECK(ed->text_width() == 0.0F);
	CHECK(ed->content_height() <= 300.0F);
	CHECK(ed->scroll_x() == 0.0F);
	REQUIRE(ed->paste("короткая строка"));
	CHECK(ed->text_width() > 0.0F);
}
