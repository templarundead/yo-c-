// Окно настроек: состав строк, попадание кликов и размеры панели.

#include "yo/optionspanel.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <vector>

using namespace yo;

namespace {

OptionsState sample_state()
{
	OptionsState state;
	state.always_ask = true;
	state.mark = false;
	state.font_size = 17.0F;
	state.dictionary = "yox.dat";
	state.regex_dictionary = "yo_regexpr.dat";
	return state;
}

} // namespace

TEST_CASE("настройки: строки и их значения", "[options]")
{
	const OptionsLayout layout = layout_options(sample_state(), 1000.0F, 800.0F, 15.0F);
	REQUIRE(layout.visible);
	REQUIRE(layout.rows.size() == 9); // 6 галочек + размер + два словаря

	CHECK(layout.rows[0].what == OptionChoice::always_ask);
	CHECK(layout.rows[0].check);
	CHECK(layout.rows[0].checked); // состояние переносится в раскладку

	CHECK(layout.rows[2].what == OptionChoice::mark);
	CHECK_FALSE(layout.rows[2].checked);

	CHECK(layout.rows[6].what == OptionChoice::dictionary);
	CHECK(layout.rows[6].path);
	CHECK(layout.rows[6].value == "yox.dat");

	CHECK(layout.rows[8].what == OptionChoice::font_smaller);
	CHECK(layout.rows[8].value == "17");
	CHECK(layout.title == "Настройки");
}

TEST_CASE("настройки: панель по центру и не шире окна", "[options]")
{
	const OptionsLayout wide = layout_options(sample_state(), 1200.0F, 800.0F, 15.0F);
	CHECK(wide.w <= 1200.0F);
	CHECK(wide.x > 0.0F);
	CHECK(wide.y > 0.0F);
	// По центру: поля слева и справа равны.
	CHECK(std::abs((wide.x) - (1200.0F - wide.x - wide.w)) < 1.0F);

	// Узкое окно — панель сжимается, но не выходит за края.
	const OptionsLayout narrow = layout_options(sample_state(), 320.0F, 240.0F, 15.0F);
	CHECK(narrow.x >= 0.0F);
	CHECK(narrow.x + narrow.w <= 320.0F);
	CHECK(narrow.y >= 0.0F);
}

TEST_CASE("настройки: клик по строке и по кнопкам", "[options]")
{
	const OptionsLayout layout = layout_options(sample_state(), 1000.0F, 800.0F, 15.0F);

	const OptionRowBox& row = layout.rows[3];
	const float cx = row.x + row.w * 0.5F;
	const float cy = row.y + row.h * 0.5F;
	const int hit = layout.row_at(cx, cy);
	REQUIRE(hit >= 0);
	CHECK(layout.rows[static_cast<std::size_t>(hit)].what == OptionChoice::propose_last);
	CHECK(layout.row_at(layout.x - 20.0F, layout.y - 20.0F) == -1);

	// Кнопки «−»/«+» — рядом со значением размера.
	int plus = -1;
	int minus = -1;
	for (std::size_t i = 0; i < layout.buttons.size(); ++i)
	{
		if (layout.buttons[i].what == OptionChoice::font_bigger)
			plus = static_cast<int>(i);
		if (layout.buttons[i].what == OptionChoice::font_smaller)
			minus = static_cast<int>(i);
	}
	REQUIRE(plus >= 0);
	REQUIRE(minus >= 0);
	const OptionButtonBox& plus_box = layout.buttons[static_cast<std::size_t>(plus)];
	CHECK(layout.button_at(plus_box.x + 2.0F, plus_box.y + 2.0F) == plus);
	CHECK(plus_box.x > layout.buttons[static_cast<std::size_t>(minus)].x); // «+» правее «−»

	// Кнопки действий не наезжают друг на друга и лежат внутри окна.
	std::vector<const OptionButtonBox*> actions;
	for (const OptionButtonBox& b : layout.buttons)
	{
		if (b.what == OptionChoice::apply || b.what == OptionChoice::apply_remember ||
			b.what == OptionChoice::cancel)
			actions.push_back(&b);
	}
	REQUIRE(actions.size() == 3);
	for (const OptionButtonBox* b : actions)
	{
		CHECK(b->x >= layout.x);
		CHECK(b->x + b->w <= layout.x + layout.w);
		CHECK(b->y + b->h <= layout.y + layout.h);
	}
	// Кнопки идут слева направо в порядке «Применить», «Запомнить», «Отмена»
	// (раскладка строит их справа налево).
	std::sort(actions.begin(), actions.end(),
			  [](const OptionButtonBox* a, const OptionButtonBox* b) { return a->x < b->x; });
	CHECK(actions[0]->what == OptionChoice::apply);
	CHECK(actions[1]->what == OptionChoice::apply_remember);
	CHECK(actions[2]->what == OptionChoice::cancel);
	CHECK(actions[0]->x + actions[0]->w <= actions[1]->x + 0.5F);
	CHECK(actions[1]->x + actions[1]->w <= actions[2]->x + 0.5F);
}

TEST_CASE("настройки: размер шрифта без хвостов", "[options]")
{
	CHECK(options_font_text(15.0F) == "15");
	CHECK(options_font_text(15.4F) == "15");
	CHECK(options_font_text(16.0F) == "16");
	CHECK(options_font_text(0.0F) == "1");
}
