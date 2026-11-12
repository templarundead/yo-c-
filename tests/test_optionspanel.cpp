// Окно настроек: состав строк по вкладкам (сверка с OptionsPage.dfm
// оригинала), попадание кликов и размеры панели.

#include "yo/optionspanel.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <string>
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

// Строки раскладки без заголовков вкладок.
std::vector<const OptionRowBox*> content_rows(const OptionsLayout& layout)
{
	std::vector<const OptionRowBox*> out;
	for (const OptionRowBox& r : layout.rows)
		if (!r.tab)
			out.push_back(&r);
	return out;
}

std::vector<std::string> labels(const OptionsLayout& layout)
{
	std::vector<std::string> out;
	for (const OptionRowBox* r : content_rows(layout))
		out.push_back(r->label);
	return out;
}

const OptionRowBox* find_row(const OptionsLayout& layout, OptionChoice what)
{
	for (const OptionRowBox& r : layout.rows)
		if (r.what == what)
			return &r;
	return nullptr;
}

} // namespace

TEST_CASE("настройки: вкладки как в оригинале", "[options]")
{
	const OptionsLayout layout = layout_options(sample_state(), 1200.0F, 900.0F, 15.0F);
	REQUIRE(layout.visible);
	REQUIRE(layout.rows.size() >= 3);

	// Три заголовка вкладок идут первыми, слева направо.
	CHECK(layout.rows[0].tab);
	CHECK(layout.rows[0].what == OptionChoice::tab_yofy);
	CHECK(layout.rows[0].label == "Ёфикация");
	CHECK(layout.rows[0].tab_active);
	CHECK(layout.rows[1].what == OptionChoice::tab_editor);
	CHECK(layout.rows[1].label == "Редактор");
	CHECK_FALSE(layout.rows[1].tab_active);
	CHECK(layout.rows[2].what == OptionChoice::tab_dicts);
	CHECK(layout.rows[0].x + layout.rows[0].w <= layout.rows[1].x);
	CHECK(layout.rows[1].x + layout.rows[1].w <= layout.rows[2].x);

	// Строки вкладки начинаются ниже заголовков.
	REQUIRE(!content_rows(layout).empty());
	CHECK(content_rows(layout).front()->y >= layout.rows[0].y + layout.rows[0].h);
	CHECK(layout.title == "Опции");
}

TEST_CASE("настройки: вкладка «Ёфикация» — пункты и подписи оригинала", "[options]")
{
	const OptionsLayout layout = layout_options(sample_state(), 1200.0F, 900.0F, 15.0F);
	// Порядок и текст — один в один с OptionsPage.dfm (с опечаткой оригинала
	// в «подверждать»).
	const std::vector<std::string> expected = {
		"Количество строк промотки:",
		"Проверять расстановку \"ё\"",
		"Только бесспорные замены",
		"Только интерактивные замены",
		"Всегда подтверждать замену",
		"Всегда подверждать замену для сокращений",
		"Всегда подтверждать замену для имён собственных",
		"Всегда подтверждать замену для оборванных слов",
		"Предлагать последний вариант замены",
		"Использование словаря регулярных выражений",
		"Учитывать формат \"FictionBook\"",
		"Быстрая промотка",
	};
	CHECK(labels(layout) == expected);

	const std::vector<OptionChoice> order = {
		OptionChoice::lines_below,	OptionChoice::check_yo,
		OptionChoice::no_var_only,
		OptionChoice::var_only,		OptionChoice::always_ask,
		OptionChoice::to_confirm_abbr, OptionChoice::to_confirm_cap,
		OptionChoice::to_confirm_ellipsis, OptionChoice::propose_last,
		OptionChoice::regexs,		OptionChoice::fb_format,
		OptionChoice::fast_scroll,
	};
	std::size_t i = 0;
	for (const OptionRowBox* row : content_rows(layout))
	{
		REQUIRE(i < order.size());
		CHECK(row->what == order[i]);
		++i;
	}

	// Галочки: состояние из OptionsState переносится в раскладку.
	const OptionRowBox* ask = find_row(layout, OptionChoice::always_ask);
	REQUIRE(ask != nullptr);
	CHECK(ask->check);
	CHECK(ask->checked);
	CHECK_FALSE(find_row(layout, OptionChoice::var_only)->checked);
	// По умолчанию включены CheckYo, ToConfirmAbbr, ToConfirmEllipsis, ProposeLast, RegExprs.
	CHECK(find_row(layout, OptionChoice::check_yo)->checked);
	CHECK(find_row(layout, OptionChoice::to_confirm_abbr)->checked);
	CHECK(find_row(layout, OptionChoice::to_confirm_ellipsis)->checked);
	CHECK(find_row(layout, OptionChoice::propose_last)->checked);
	CHECK(find_row(layout, OptionChoice::regexs)->checked);
	CHECK_FALSE(find_row(layout, OptionChoice::fb_format)->checked);
	CHECK_FALSE(find_row(layout, OptionChoice::fast_scroll)->checked);

	// «Количество строк промотки» — число и кнопки «−»/«+».
	const OptionRowBox* lines = find_row(layout, OptionChoice::lines_below);
	REQUIRE(lines != nullptr);
	CHECK_FALSE(lines->check);
	CHECK(lines->value == "2");
	CHECK_FALSE(lines->path);

	int minus = -1;
	int plus = -1;
	for (std::size_t b = 0; b < layout.buttons.size(); ++b)
	{
		if (layout.buttons[b].what == OptionChoice::lines_below_minus)
			minus = static_cast<int>(b);
		if (layout.buttons[b].what == OptionChoice::lines_below_plus)
			plus = static_cast<int>(b);
	}
	REQUIRE(minus >= 0);
	REQUIRE(plus >= 0);
	// Кнопки стоят в строке числа и «+» правее «−».
	CHECK(layout.buttons[static_cast<std::size_t>(plus)].x >
		  layout.buttons[static_cast<std::size_t>(minus)].x);
	CHECK(layout.buttons[static_cast<std::size_t>(plus)].y >= lines->y);
	CHECK(layout.buttons[static_cast<std::size_t>(plus)].y + layout.buttons[static_cast<std::size_t>(plus)].h <=
		  lines->y + lines->h);
}

TEST_CASE("настройки: вкладка «Редактор» — пункты и подписи оригинала", "[options]")
{
	OptionsState state = sample_state();
	state.tab = 1;
	const OptionsLayout layout = layout_options(state, 1200.0F, 900.0F, 15.0F);
	const std::vector<std::string> expected = {
		"Помечать исправленное",
		"Заворачивать текст (\"Word wrap\")",
		"Запоминать последний файл",
		"Шрифт",
		"Цвет",
		"Фон",
		"Автораспознавать формат Unicode",
		"Показывать панель инструментов",
		"Не подтверждать закрытие программы",
	};
	CHECK(labels(layout) == expected);

	CHECK(layout.rows[1].tab_active);
	CHECK_FALSE(layout.rows[0].tab_active);

	// Значения: цвет пометки/фона — из палитры, шрифт — число.
	CHECK(find_row(layout, OptionChoice::mark_color)->value == options_color_at(0).name);
	CHECK(find_row(layout, OptionChoice::back_mark_color)->value == options_color_at(1).name);
	CHECK(find_row(layout, OptionChoice::font_size)->value == "17");
	CHECK_FALSE(find_row(layout, OptionChoice::mark)->checked); // mark = false в состоянии

	int plus = -1;
	for (std::size_t b = 0; b < layout.buttons.size(); ++b)
		if (layout.buttons[b].what == OptionChoice::font_bigger)
			plus = static_cast<int>(b);
	REQUIRE(plus >= 0);
	const OptionRowBox* font = find_row(layout, OptionChoice::font_size);
	CHECK(layout.buttons[static_cast<std::size_t>(plus)].y >= font->y);
}

TEST_CASE("настройки: вкладка «Словари» — пути", "[options]")
{
	OptionsState state = sample_state();
	state.tab = 2;
	const OptionsLayout layout = layout_options(state, 1200.0F, 900.0F, 15.0F);
	const std::vector<std::string> expected = {
		"Основной словарь",
		"Словарь регулярных выражений",
		"Ёфицировать без вопросов",
	};
	CHECK(labels(layout) == expected);
	CHECK(layout.rows[2].tab_active);

	const OptionRowBox* dict = find_row(layout, OptionChoice::dictionary);
	REQUIRE(dict != nullptr);
	CHECK(dict->path);
	CHECK(dict->value == "yox.dat");
	CHECK(find_row(layout, OptionChoice::regex_dictionary)->value == "yo_regexpr.dat");

	// Переключение вкладки не меняет размер окна (чтобы панель не «прыгала»).
	const OptionsState yofy = sample_state();
	const OptionsLayout a = layout_options(yofy, 1200.0F, 900.0F, 15.0F);
	CHECK(std::abs(a.w - layout.w) < 0.5F);
}

TEST_CASE("настройки: панель по центру и не шире окна", "[options]")
{
	const OptionsLayout wide = layout_options(sample_state(), 1200.0F, 900.0F, 15.0F);
	CHECK(wide.w <= 1200.0F);
	CHECK(wide.x > 0.0F);
	CHECK(wide.y > 0.0F);
	CHECK(std::abs((wide.x) - (1200.0F - wide.x - wide.w)) < 1.0F);

	// Узкое окно — панель сжимается, но не выходит за края.
	const OptionsLayout narrow = layout_options(sample_state(), 320.0F, 240.0F, 15.0F);
	CHECK(narrow.x >= 0.0F);
	CHECK(narrow.x + narrow.w <= 320.0F);
	CHECK(narrow.y >= 0.0F);

	// Нулевая область — окна нет.
	CHECK_FALSE(layout_options(sample_state(), 0.0F, 900.0F, 15.0F).visible);
}

TEST_CASE("настройки: клик по строке и по кнопкам", "[options]")
{
	const OptionsLayout layout = layout_options(sample_state(), 1200.0F, 900.0F, 15.0F);

	const OptionRowBox* row = find_row(layout, OptionChoice::propose_last);
	REQUIRE(row != nullptr);
	const int hit = layout.row_at(row->x + row->w * 0.5F, row->y + row->h * 0.5F);
	REQUIRE(hit >= 0);
	CHECK(layout.rows[static_cast<std::size_t>(hit)].what == OptionChoice::propose_last);
	CHECK(layout.row_at(layout.x - 20.0F, layout.y - 20.0F) == -1);

	// Клик по заголовку вкладки возвращает саму вкладку.
	const int tab_hit = layout.row_at(layout.rows[1].x + 2.0F, layout.rows[1].y + 2.0F);
	REQUIRE(tab_hit >= 0);
	CHECK(layout.rows[static_cast<std::size_t>(tab_hit)].what == OptionChoice::tab_editor);

	// Кнопки действий: «Применить», «Отменить», «Применить и запомнить»
	// слева направо (порядок bOK / bCancel / bOKSave оригинала).
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
	std::sort(actions.begin(), actions.end(), [](const OptionButtonBox* a, const OptionButtonBox* b)
			  { return a->x < b->x; });
	CHECK(actions[0]->what == OptionChoice::apply);
	CHECK(actions[1]->what == OptionChoice::cancel);
	CHECK(actions[2]->what == OptionChoice::apply_remember);
	CHECK(actions[0]->label == "Применить");
	CHECK(actions[1]->label == "Отменить");
	CHECK(actions[2]->label == "Применить и запомнить");
	CHECK(actions[0]->x + actions[0]->w <= actions[1]->x + 0.5F);
	CHECK(actions[1]->x + actions[1]->w <= actions[2]->x + 0.5F);

	// Кнопки не пересекаются между собой и со строками.
	for (std::size_t i = 0; i < layout.buttons.size(); ++i)
	{
		const OptionButtonBox& a = layout.buttons[i];
		for (std::size_t j = i + 1; j < layout.buttons.size(); ++j)
		{
			const OptionButtonBox& b = layout.buttons[j];
			const bool overlap = a.x < b.x + b.w && b.x < a.x + a.w && a.y < b.y + b.h &&
								 b.y < a.y + a.h;
			CHECK_FALSE(overlap);
		}
	}
}

TEST_CASE("настройки: палитра цветов пометки", "[options]")
{
	REQUIRE(options_colors().size() >= 4);
	// Умолчания оригинала: ColorMark=clBlue, ColorBackMark=clYellow.
	CHECK(options_color_at(0).name == "синий");
	CHECK(options_color_at(0).rgb == 0x00FF0000U);
	CHECK(options_color_at(1).name == "жёлтый");
	CHECK(options_color_at(1).rgb == 0x0000FFFFU);

	// Переход по кругу и отрицательные индексы (после «минус» без проверки).
	const int count = static_cast<int>(options_colors().size());
	CHECK(options_next_color(0) == 1);
	CHECK(options_next_color(count - 1) == 0);
	CHECK(options_next_color(-1) == 0);
	CHECK(options_color_at(count).name == options_color_at(0).name);
	CHECK(options_color_at(-count).name == options_color_at(0).name);

	OptionsState state;
	state.mark_color = count - 1;
	state.tab = 1;
	const OptionsLayout layout = layout_options(state, 1200.0F, 900.0F, 15.0F);
	CHECK(find_row(layout, OptionChoice::mark_color)->value == options_color_at(count - 1).name);
}

TEST_CASE("настройки: размер шрифта без хвостов", "[options]")
{
	CHECK(options_font_text(15.0F) == "15");
	CHECK(options_font_text(15.4F) == "15");
	CHECK(options_font_text(16.0F) == "16");
	CHECK(options_font_text(0.0F) == "1");
}
