#include "yo/optionspanel.hpp"

#include <algorithm>
#include <array>
#include <cmath>

namespace yo {

namespace {

constexpr float kPad = 14.0F;	 // поля окна
constexpr float kRowGap = 2.0F;	 // зазор между строками
constexpr float kPanelMinW = 420.0F;

// Приблизительная ширина строки: точные метрики есть только у GUI, а для
// раскладки достаточно пропорции — тесты проверяют, что кнопки не
// наезжают друг на друга и лежат внутри окна.
float text_width(std::string_view text, float line_h)
{
	float w = 0.0F;
	for (char c : text)
		w += (static_cast<unsigned char>(c) < 0x80U) ? line_h * 0.52F : line_h * 0.58F;
	return w;
}

std::string trim_size(float size)
{
	const int rounded = static_cast<int>(std::lround(size));
	return std::to_string(std::max(1, rounded));
}

// Палитра цветов замен — как ColorMark/ColorBackMark в [Colors] оригинала.
constexpr std::array<OptionsColor, 6> kColors = { {
	{ 0x00FF0000U, "синий" },
	{ 0x0000FFFFU, "жёлтый" },
	{ 0x00008000U, "зелёный" },
	{ 0x000000FFU, "красный" },
	{ 0x00FFFF00U, "голубой" },
	{ 0x00808080U, "серый" },
} };

// Строка вкладки в описании раскладки.
struct RowDef
{
	OptionChoice what;
	std::string label;
	std::string value;
	bool check = false;
	bool checked = false;
	bool path = false;
};

// Строки вкладки «Ёфикация» — порядок и подписи из OptionsPage.dfm.
std::vector<RowDef> yofy_rows(const OptionsState& s)
{
	// Порядок контролов — как они объявлены в OptionsPage.dfm: сначала число
	// строк промотки, затем галочки.
	return {
		{ OptionChoice::lines_below, "Количество строк промотки:", std::to_string(s.lines_below),
		  false, false, false },
		{ OptionChoice::check_yo, "Проверять расстановку \"ё\"", {}, true, s.check_yo, false },
		{ OptionChoice::no_var_only, "Только бесспорные замены", {}, true, s.no_var_only, false },
		{ OptionChoice::var_only, "Только интерактивные замены", {}, true, s.var_only, false },
		{ OptionChoice::always_ask, "Всегда подтверждать замену", {}, true, s.always_ask, false },
		// Опечатка оригинала («подверждать») сохранена намеренно: подписи
		// перенесены один в один.
		{ OptionChoice::to_confirm_abbr, "Всегда подверждать замену для сокращений", {}, true,
		  s.to_confirm_abbr, false },
		{ OptionChoice::to_confirm_cap, "Всегда подтверждать замену для имён собственных", {}, true,
		  s.to_confirm_cap, false },
		{ OptionChoice::to_confirm_ellipsis, "Всегда подтверждать замену для оборванных слов", {},
		  true, s.to_confirm_ellipsis, false },
		{ OptionChoice::propose_last, "Предлагать последний вариант замены", {}, true, s.propose_last,
		  false },
		{ OptionChoice::regexs, "Использование словаря регулярных выражений", {}, true, s.regexs,
		  false },
		{ OptionChoice::fb_format, "Учитывать формат \"FictionBook\"", {}, true, s.fb_format, false },
		{ OptionChoice::fast_scroll, "Быстрая промотка", {}, true, s.fast_scroll, false },
	};
}

// Строки вкладки «Редактор» — порядок и подписи из OptionsPage.dfm.
std::vector<RowDef> editor_rows(const OptionsState& s)
{
	// Порядок — как в OptionsPage.dfm (TabSheet2).
	return {
		{ OptionChoice::mark, "Помечать исправленное", {}, true, s.mark, false },
		{ OptionChoice::word_wrap, "Заворачивать текст (\"Word wrap\")", {}, true, s.word_wrap, false },
		{ OptionChoice::recall_last_file, "Запоминать последний файл", {}, true, s.recall_last_file,
		  false },
		{ OptionChoice::font_size, "Шрифт", trim_size(s.font_size), false, false, false },
		{ OptionChoice::mark_color, "Цвет", options_color_at(s.mark_color).name, false, false, false },
		{ OptionChoice::back_mark_color, "Фон", options_color_at(s.back_mark_color).name, false, false,
		  false },
		{ OptionChoice::auto_unicode, "Автораспознавать формат Unicode", {}, true, s.auto_unicode,
		  false },
		{ OptionChoice::show_toolbar, "Показывать панель инструментов", {}, true, s.show_toolbar,
		  false },
		{ OptionChoice::to_confirm_close, "Не подтверждать закрытие программы", {}, true,
		  s.to_confirm_close, false },
	};
}

// Вкладка «Словари» — наше дополнение (в оригинале путей в опциях нет).
std::vector<RowDef> dict_rows(const OptionsState& s)
{
	return {
		{ OptionChoice::dictionary, "Основной словарь", s.dictionary, false, false, true },
		{ OptionChoice::regex_dictionary, "Словарь регулярных выражений", s.regex_dictionary, false,
		  false, true },
		{ OptionChoice::silent_yofy, "Ёфицировать без вопросов", {}, true, s.silent_yofy, false },
	};
}

const std::vector<std::string> kTabTitles = { "Ёфикация", "Редактор", "Словари" };

} // namespace

std::span<const OptionsColor> options_colors() { return kColors; }

const OptionsColor& options_color_at(int index)
{
	const int count = static_cast<int>(kColors.size());
	int i = index % count;
	if (i < 0)
		i += count;
	return kColors[static_cast<std::size_t>(i)];
}

int options_next_color(int current)
{
	const int count = static_cast<int>(kColors.size());
	return ((current % count) + 1) % count;
}

std::string options_font_text(float size) { return trim_size(size); }

int OptionsLayout::row_at(float px, float py) const
{
	for (std::size_t i = 0; i < rows.size(); ++i)
	{
		const OptionRowBox& r = rows[i];
		if (px >= r.x && px <= r.x + r.w && py >= r.y && py <= r.y + r.h)
			return static_cast<int>(i);
	}
	return -1;
}

int OptionsLayout::button_at(float px, float py) const
{
	for (std::size_t i = 0; i < buttons.size(); ++i)
	{
		const OptionButtonBox& b = buttons[i];
		if (px >= b.x && px <= b.x + b.w && py >= b.y && py <= b.y + b.h)
			return static_cast<int>(i);
	}
	return -1;
}

OptionsLayout layout_options(const OptionsState& state, float width, float height, float line_h)
{
	OptionsLayout out;
	if (width <= 0.0F || height <= 0.0F)
		return out;

	const float lh = std::max(10.0F, line_h);
	const float row_h = lh + 8.0F;
	const float button_h = lh + 10.0F;
	const float tab_h = lh + 12.0F;

	const int tab = std::clamp(state.tab, 0, 2);
	const std::vector<RowDef> rows =
		tab == 0 ? yofy_rows(state) : (tab == 1 ? editor_rows(state) : dict_rows(state));

	// Ширина окна: самая длинная строка любой вкладки плюс значение справа,
	// чтобы окно не «прыгало» при переключении вкладок.
	float content = text_width(out.title, lh) * 1.4F;
	const auto measure = [&](const std::vector<RowDef>& defs)
	{
		for (const RowDef& d : defs)
			content = std::max(content, text_width(d.label, lh) + text_width(d.value, lh) + 190.0F);
	};
	measure(yofy_rows(state));
	measure(editor_rows(state));
	measure(dict_rows(state));

	out.w = std::clamp(content + 2.0F * kPad, std::min(kPanelMinW, width - 16.0F), width - 16.0F);
	out.w = std::max(out.w, 260.0F);

	const float rows_h = static_cast<float>(rows.size()) * (row_h + kRowGap);
	out.h = kPad + lh * 1.8F + tab_h + kRowGap + rows_h + kPad + button_h + kPad;
	out.h = std::min(out.h, std::max(140.0F, height - 16.0F));
	out.x = std::max(8.0F, (width - out.w) * 0.5F);
	out.y = std::max(8.0F, (height - out.h) * 0.5F);
	out.visible = true;

	// Заголовки вкладок: «Ёфикация» / «Редактор» / «Словари» слева направо.
	float tab_x = out.x + kPad;
	const float tab_y = out.y + kPad + lh * 1.8F;
	const std::array<OptionChoice, 3> tab_choices = { OptionChoice::tab_yofy,
													  OptionChoice::tab_editor,
													  OptionChoice::tab_dicts };
	for (std::size_t i = 0; i < kTabTitles.size(); ++i)
	{
		const float w = text_width(kTabTitles[i], lh) + 26.0F;
		OptionRowBox row;
		row.what = tab_choices[i];
		row.label = kTabTitles[i];
		row.tab = true;
		row.tab_active = static_cast<int>(i) == tab;
		row.checked = row.tab_active;
		row.x = tab_x;
		row.y = tab_y;
		row.w = w;
		row.h = tab_h;
		out.rows.push_back(std::move(row));
		tab_x += w + 6.0F;
	}

	float y = tab_y + tab_h + kRowGap;
	for (const RowDef& d : rows)
	{
		OptionRowBox row;
		row.what = d.what;
		row.label = d.label;
		row.value = d.value;
		row.check = d.check;
		row.checked = d.checked;
		row.path = d.path;
		row.x = out.x + kPad;
		row.y = y;
		row.w = out.w - 2.0F * kPad;
		row.h = row_h;
		out.rows.push_back(std::move(row));
		y += row_h + kRowGap;
	}

	// «−»/«+» справа в строках числа: строки промотки и размер шрифта.
	const float small = std::max(22.0F, lh + 10.0F);
	const float row_right = out.x + out.w - kPad;
	const auto stepper = [&](const OptionRowBox& row, OptionChoice minus, OptionChoice plus)
	{
		// Кнопки не должны вылезать из строки: высота ужимается до строки.
		const float bh = std::min(small, row.h);
		const float by = row.y + (row.h - bh) * 0.5F;
		out.buttons.push_back(OptionButtonBox{ plus, "+", row_right - small, by, small, bh });
		out.buttons.push_back(
			OptionButtonBox{ minus, "−", row_right - 2.0F * small - 4.0F, by, small, bh });
	};
	for (const OptionRowBox& row : out.rows)
	{
		if (row.what == OptionChoice::lines_below)
			stepper(row, OptionChoice::lines_below_minus, OptionChoice::lines_below_plus);
		else if (row.what == OptionChoice::font_size)
			stepper(row, OptionChoice::font_smaller, OptionChoice::font_bigger);
	}

	// Кнопки внизу — как bOK, bCancel, bOKSave оригинала (слева направо).
	const std::vector<std::pair<OptionChoice, std::string>> actions = {
		{ OptionChoice::apply, "Применить" },
		{ OptionChoice::cancel, "Отменить" },
		{ OptionChoice::apply_remember, "Применить и запомнить" },
	};
	const float gap = 8.0F;
	float total = 0.0F;
	for (const auto& [what, label] : actions)
		total += text_width(label, lh) + 28.0F;
	total += gap * static_cast<float>(actions.size() - 1);
	const float fit = total > out.w - 2.0F * kPad ? (out.w - 2.0F * kPad) / total : 1.0F;

	float bx = out.x + kPad;
	const float by = out.y + out.h - kPad - button_h;
	for (const auto& [what, label] : actions)
	{
		const float bw = (text_width(label, lh) + 28.0F) * fit;
		out.buttons.push_back(
			OptionButtonBox{ what, label, bx, by, bw, button_h });
		bx += bw + gap * fit;
	}
	return out;
}

} // namespace yo
