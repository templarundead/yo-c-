// Панель вопросов ёфикации: тексты, раскладка кнопок, подрезка строк.

#include "yo/askpanel.hpp"
#include "yo/codec.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <string>
#include <vector>

using namespace yo;

namespace {

UString u(std::string_view utf8)
{
	return decode_line({ reinterpret_cast<const std::uint8_t*>(utf8.data()), utf8.size() },
					   Encoding::utf8);
}

// Моноширинный «шрифт»: ширина = число байт * 8.
float mono(std::string_view text) { return static_cast<float>(text.size()) * 8.0F; }

AskLayoutInput base_input()
{
	AskLayoutInput in;
	in.client_w = 1000.0F;
	in.client_h = 600.0F;
	in.status_h = 20.0F;
	in.padding = 6.0F;
	in.line_height = 18.0F;
	in.char_width = 8.0F;
	in.variants = "1  Е: все          2  Ё: всё";
	in.reason = "выберите написание";
	in.context = "стр. 12: и вот мы все еще не пришли";
	return in;
}

bool well_formed_utf8(std::string_view text)
{
	std::size_t i = 0;
	while (i < text.size())
	{
		const auto c = static_cast<unsigned char>(text[i]);
		std::size_t extra = 0;
		if (c < 0x80U)
			extra = 0;
		else if ((c & 0xE0U) == 0xC0U)
			extra = 1;
		else if ((c & 0xF0U) == 0xE0U)
			extra = 2;
		else if ((c & 0xF8U) == 0xF0U)
			extra = 3;
		else
			return false;
		if (i + extra >= text.size() && extra > 0)
			return false;
		for (std::size_t k = 1; k <= extra; ++k)
		{
			if ((static_cast<unsigned char>(text[i + k]) & 0xC0U) != 0x80U)
				return false;
		}
		i += extra + 1;
	}
	return true;
}

} // namespace

TEST_CASE("панель вопросов: тексты вариантов и причины", "[askpanel]")
{
	AskRequest req;
	req.word1 = u("все");
	req.word2 = u("всё");
	AskTexts texts = make_ask_texts(req);
	CHECK(texts.variants == "1  Е: все          2  Ё: всё");
	// Заголовок панели — как в оригинале («Выберите верный вариант»).
	CHECK(texts.reason == "Выберите верный вариант");

	SECTION("без варианта с ё второй кнопки в тексте нет")
	{
		req.word2 = UString();
		req.wrong_yo = true;
		texts = make_ask_texts(req);
		CHECK(texts.variants == "1  Е: все");
		CHECK(texts.reason.find("Е") != std::string::npos);
	}
	SECTION("подсказка «здесь должна быть Ё»")
	{
		req.wrong_ye = true;
		texts = make_ask_texts(req);
		CHECK(texts.reason.find("Ё") != std::string::npos);
	}
}

TEST_CASE("панель вопросов: ответы переводятся в AskAnswer", "[askpanel]")
{
	CHECK(to_answer(AskChoice::ye) == AskAnswer::ye);
	CHECK(to_answer(AskChoice::yo) == AskAnswer::yo);
	CHECK(to_answer(AskChoice::ye_all) == AskAnswer::ye_all);
	CHECK(to_answer(AskChoice::yo_all) == AskAnswer::yo_all);
	CHECK(to_answer(AskChoice::skip) == AskAnswer::skip);
	CHECK(to_answer(AskChoice::undo) == AskAnswer::undo);
	CHECK(to_answer(AskChoice::cancel) == AskAnswer::cancel);
}

TEST_CASE("панель вопросов: кнопки и строки в широком окне", "[askpanel]")
{
	const AskLayout layout = layout_ask_panel(base_input(), mono);
	REQUIRE(layout.visible);
	REQUIRE(layout.buttons.size() == 7);
	REQUIRE(layout.rows.size() == 3);

	CHECK(layout.x >= 6.0F);
	CHECK(layout.y >= 0.0F);
	CHECK(layout.y + layout.h <= 600.0F - 20.0F + 0.01F);
	CHECK(layout.w <= 1000.0F - 12.0F + 0.01F);

	// Все кнопки внутри панели и не наезжают друг на друга в ряду.
	for (std::size_t i = 0; i < layout.buttons.size(); ++i)
	{
		const AskButtonBox& b = layout.buttons[i];
		CHECK(b.x >= layout.x);
		CHECK(b.x + b.w <= layout.x + layout.w + 0.01F);
		CHECK(b.y >= layout.y);
		CHECK(b.y + b.h <= layout.y + layout.h + 0.01F);
		CHECK((b.label.rfind(std::to_string(i + 1), 0) == 0 || i == layout.buttons.size() - 1));
		if (i > 0 && layout.buttons[i - 1].y == b.y)
			CHECK(layout.buttons[i - 1].x + layout.buttons[i - 1].w <= b.x + 0.01F);
	}

	// Строки идут сверху вниз без наложений, кнопки — ниже строк.
	CHECK(layout.rows[0].y < layout.rows[1].y);
	CHECK(layout.rows[1].y < layout.rows[2].y);
	CHECK(layout.rows[2].y + layout.rows[2].h <= layout.buttons.front().y + 0.01F);

	CHECK(layout.index_of(AskChoice::ye) == 0);
	CHECK(layout.index_of(AskChoice::yo) == 1);
	CHECK(layout.index_of(AskChoice::cancel) == 6);
}

TEST_CASE("панель вопросов: в узком окне кнопки переносятся", "[askpanel]")
{
	AskLayoutInput in = base_input();
	in.client_w = 320.0F;
	const AskLayout layout = layout_ask_panel(in, mono);
	REQUIRE(layout.visible);

	int rows_of_buttons = 0;
	float previous_y = -1.0F;
	for (const AskButtonBox& b : layout.buttons)
	{
		if (b.y != previous_y)
		{
			++rows_of_buttons;
			previous_y = b.y;
		}
		CHECK(b.x >= layout.x);
		CHECK(b.x + b.w <= layout.x + layout.w + 0.01F);
	}
	CHECK(rows_of_buttons >= 2);
	// Ряды идут сверху вниз.
	float last_y = -1.0F;
	for (const AskButtonBox& b : layout.buttons)
	{
		if (b.y != last_y)
		{
			CHECK(b.y > last_y);
			last_y = b.y;
		}
	}
}

TEST_CASE("панель вопросов: длинная строка контекста подрезается", "[askpanel]")
{
	AskLayoutInput in = base_input();
	in.context = "стр. 7: ";
	for (int i = 0; i < 200; ++i)
		in.context += "ещё ";
	const AskLayout layout = layout_ask_panel(in, mono);
	REQUIRE(layout.rows.size() == 3);
	const std::string& text = layout.rows[2].text;
	CHECK(text.ends_with("…"));
	CHECK(mono(text) <= 1000.0F - 12.0F + 8.0F);
	CHECK(well_formed_utf8(text));
}

TEST_CASE("панель вопросов: невысокое окно — панель не уезжает за верх", "[askpanel]")
{
	AskLayoutInput in = base_input();
	in.client_w = 320.0F;
	in.client_h = 220.0F;
	const AskLayout layout = layout_ask_panel(in, mono);
	REQUIRE(layout.visible);
	CHECK(layout.y >= 0.0F);
	CHECK(layout.h > 0.0F);
}

TEST_CASE("панель вопросов: контекст из нескольких строк", "[askpanel]")
{
	// В панели — всё предложение: оно переносится по ширине и занимает
	// несколько строк, панель обязана вырасти, а не обрезать его.
	AskLayoutInput in = base_input();
	in.context = "и вот мы все еще не пришли\nдомой, а дождь всё идёт";
	const AskLayout layout = layout_ask_panel(in, mono);
	REQUIRE(layout.visible);
	CHECK(layout.rows.size() >= 4); // варианты, причина и две строки контекста
	std::size_t context_rows = 0;
	for (const AskLayout::Row& row : layout.rows)
	{
		if (row.text.find("домой") != std::string::npos || row.text.find("не пришли") != std::string::npos)
			++context_rows;
	}
	CHECK(context_rows >= 2);
}

TEST_CASE("панель вопросов: кнопки достаточно крупные для мыши", "[askpanel]")
{
	AskLayoutInput in = base_input();
	const AskLayout layout = layout_ask_panel(in, mono);
	REQUIRE(!layout.buttons.empty());
	for (const AskButtonBox& b : layout.buttons)
	{
		CHECK(b.h >= in.line_height + 2.0F * 3.0F); // строка текста + отступы
		CHECK(b.w > in.char_width * 4.0F);
	}
}

TEST_CASE("панель вопросов: полоса снизу, кнопки в две колонки", "[askpanel]")
{
	// Как AskPanel оригинала: полоса во всю ширину над строкой состояния,
	// заголовок по центру, кнопки подписаны самими вариантами слова.
	AskLayoutInput in = base_input();
	in.bottom_bar = true;
	in.word1 = "передохнем";
	in.word2 = "передохнём";
	in.context = "дина нам бог послал, при таком-то передохнем: А кричит";
	const AskLayout layout = layout_ask_panel(in, mono);
	REQUIRE(layout.visible);
	CHECK(layout.bottom);
	CHECK(layout.x == 0.0F);
	CHECK(layout.w == in.client_w);
	CHECK(layout.y + layout.h == in.client_h - in.status_h);

	// Сначала предложение с обрабатываемым словом, под ним — заголовок,
	// под заголовком — кнопки (как просил пользователь).
	REQUIRE(!layout.rows.empty());
	std::size_t context_row = layout.rows.size();
	std::size_t title_row = layout.rows.size();
	for (std::size_t i = 0; i < layout.rows.size(); ++i)
	{
		if (layout.rows[i].text.find("передохнем") != std::string::npos)
			context_row = i;
		if (layout.rows[i].kind == AskRowKind::title)
			title_row = i;
	}
	REQUIRE(context_row < layout.rows.size());
	REQUIRE(title_row < layout.rows.size());
	CHECK(layout.rows[title_row].text == in.reason);
	CHECK(context_row < title_row);
	// Контекст — одной строкой (без переносов), слово видно целиком.
	CHECK(context_row + 1 == title_row);

	// Семь кнопок: две колонки по три и «Прервать» во всю ширину.
	REQUIRE(layout.buttons.size() == 7);
	const AskButtonBox* cancel_box = nullptr;
	for (const AskButtonBox& b : layout.buttons)
		if (b.choice == AskChoice::cancel)
			cancel_box = &b;
	REQUIRE(cancel_box != nullptr);
	float left_x = layout.buttons.front().x;
	for (const AskButtonBox& b : layout.buttons)
		left_x = std::min(left_x, b.x);
	CHECK(cancel_box->x == left_x);
	// Ширина «Прервать» — как у двух колонок вместе.
	float two_cols = 0.0F;
	for (const AskButtonBox& b : layout.buttons)
		if (b.choice != AskChoice::cancel)
			two_cols = std::max(two_cols, b.x + b.w - left_x);
	CHECK(cancel_box->w >= two_cols - 1.0F);
	std::vector<float> xs;
	for (const AskButtonBox& b : layout.buttons)
	{
		if (std::find(xs.begin(), xs.end(), b.x) == xs.end())
			xs.push_back(b.x);
	}
	CHECK(xs.size() == 2);

	// Подписи: слово + клавиша.
	auto label_of = [&](AskChoice choice)
	{
		for (const AskButtonBox& b : layout.buttons)
			if (b.choice == choice)
				return b.label;
		return std::string();
	};
	CHECK(label_of(AskChoice::ye) == "передохнем [1]");
	CHECK(label_of(AskChoice::yo) == "передохнём [2]");
	CHECK(label_of(AskChoice::cancel) == "Прервать [Esc]");
}

TEST_CASE("панель вопросов: полоса не вылезает на маленьком окне", "[askpanel]")
{
	// Очень маленькое окно: полоса не помещается — раскладка обязана
	// остаться видимой (плавающая панель), иначе спросить будет негде.
	AskLayoutInput in = base_input();
	in.bottom_bar = true;
	in.word1 = "передохнем";
	in.word2 = "передохнём";
	in.client_w = 360.0F;
	in.client_h = 240.0F;
	in.context = "короткая фраза";
	const AskLayout layout = layout_ask_panel(in, mono);
	REQUIRE(layout.visible);
	CHECK(layout.h > 0.0F);
	CHECK(layout.y >= 0.0F);
	CHECK(layout.y + layout.h <= in.client_h);
	for (const AskButtonBox& b : layout.buttons)
	{
		CHECK(b.x >= 0.0F);
		CHECK(b.x + b.w <= in.client_w);
		CHECK(b.y + b.h <= in.client_h);
	}
}

TEST_CASE("askpanel: байтовый диапазон слова для подсветки", "[askpanel]")
{
	// Кириллица в UTF-8 — по 2 байта на букву: смещения в кодовых точках
	// и в байтах не совпадают, поэтому пересчёт должен быть точным.
	const std::string line = "все ли всё равно";           // 16 символов
	const AskWordRange first = ask_word_bytes(line, 0, 3); // «все»
	REQUIRE(first.valid());
	CHECK(first.from == 0);
	CHECK(first.to == 6);
	CHECK(line.substr(first.from, first.to - first.from) == "все");

	const AskWordRange yo = ask_word_bytes(line, 7, 3); // «всё»
	REQUIRE(yo.valid());
	CHECK(line.substr(yo.from, yo.to - yo.from) == "всё");

	// Границы: offset за концом строки и нулевая длина — пустой диапазон.
	CHECK_FALSE(ask_word_bytes(line, line.size(), 5).valid());
	CHECK_FALSE(ask_word_bytes(line, 3, 0).valid());
	CHECK_FALSE(ask_word_bytes("", 0, 3).valid());

	// Длина больше остатка строки подрезается, а не выходит за границы.
	const AskWordRange tail = ask_word_bytes(line, 11, 100);
	REQUIRE(tail.valid());
	CHECK(tail.to <= line.size());
	CHECK(line.substr(tail.from, tail.to - tail.from) == "равно");

	// Суррогаты (4-байтовый UTF-8) считаются как один кодпоинт.
	const std::string emoji = "\xF0\x9F\x98\x80 ё";
	const AskWordRange e = ask_word_bytes(emoji, 2, 1);
	REQUIRE(e.valid());
	CHECK(emoji.substr(e.from, e.to - e.from) == "ё");
}

TEST_CASE("панель вопросов: подписи кнопок влезают целиком", "[askpanel]")
{
	// Текст кнопки рисуется тем же шрифтом, которым измерен: подпись
	// «Пропустить [5]» не должна обрезаться по краям («1ропустить [5]»).
	AskLayoutInput in = base_input();
	in.bottom_bar = true;
	in.word1 = "все";
	in.word2 = "всё";
	in.context = "Фама, знаю, мы все сошли с ума";
	const AskLayout layout = layout_ask_panel(in, mono);
	REQUIRE(layout.visible);
	REQUIRE(layout.buttons.size() == 7);
	for (const AskButtonBox& b : layout.buttons)
	{
		// Полная ширина подписи плюс поля по краям помещается в кнопку.
		CHECK(mono(b.label) + in.char_width <= b.w + 0.5F);
		CHECK(b.x >= 0.0F);
		CHECK(b.x + b.w <= in.client_w + 0.5F);
	}
}
