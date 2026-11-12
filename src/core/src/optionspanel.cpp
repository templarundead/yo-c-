#include "yo/optionspanel.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>

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

// Стандартные цвета (ColorDialog) и значения по умолчанию оригинала:
// clMark = clBlue, clBackMark = clYellow.
constexpr std::array<OptionsColor, 9> kColors = { {
	{ kColorBlue, "синий" },
	{ kColorYellow, "жёлтый" },
	{ kColorGreen, "зелёный" },
	{ kColorRed, "красный" },
	{ kColorAqua, "голубой" },
	{ kColorGray, "серый" },
	{ kColorBlack, "чёрный" },
	{ kColorWhite, "белый" },
	{ 0x00800000U, "тёмно-синий" },
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
		{ OptionChoice::font_size, "Шрифт", options_font_label(s), false, false, false },
		{ OptionChoice::mark_color, "Цвет", options_color_text(s.mark_color), false, false, false },
		{ OptionChoice::back_mark_color, "Фон", options_color_text(s.back_mark_color), false, false,
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

std::string options_color_text(std::uint32_t rgb)
{
	for (const OptionsColor& c : kColors)
		if (c.rgb == rgb)
			return c.name;
	// Любой другой цвет — как в вебе: COLORREF хранит байты BGR.
	const auto byte = [](std::uint32_t v) { return static_cast<unsigned>(v & 0xFFU); };
	char buf[16];
	std::snprintf(buf, sizeof(buf), "#%02X%02X%02X", byte(rgb), byte(rgb >> 8U), byte(rgb >> 16U));
	return std::string(buf);
}

std::uint32_t options_color_from_ini(long long value)
{
	// Предыдущие сборки yo-c- писали индекс палитры (0..5) — понимаем и его.
	if (value >= 0 && value < static_cast<long long>(kColors.size()) && value <= 5)
		return kColors[static_cast<std::size_t>(value)].rgb;
	if (value < 0 || value > 0xFFFFFFLL)
		return kColorBlack;
	return static_cast<std::uint32_t>(value);
}

bool option_enabled(const OptionsState& state, OptionChoice what)
{
	switch (what)
	{
	// OptionsPage.cbClick: AlwaysAsk выключает три «подтверждать».
	case OptionChoice::to_confirm_abbr:
	case OptionChoice::to_confirm_cap:
	case OptionChoice::to_confirm_ellipsis:
		return !state.always_ask;
	// ... и VarOnly ↔ NoVarOnly несовместимы.
	case OptionChoice::var_only:
		return !state.no_var_only;
	case OptionChoice::no_var_only:
		return !state.var_only;
	// Кнопки цветов доступны только при «Помечать исправленное».
	case OptionChoice::mark_color:
	case OptionChoice::back_mark_color:
		return state.mark;
	default:
		return true;
	}
}

void options_normalize(OptionsState& state)
{
	// Согласование значений, пришедших из ini. Приоритет — у «безопасных»
	// режимов: VarOnly («спрашивать про всё») снимается, если включено
	// «Всегда подтверждать замену» (AlwaysAsk) или «Только бесспорные
	// замены» (NoVarOnly). Взаимоисключение VarOnly ↔ AlwaysAsk — по
	// требованию к редактору: либо то, либо другое.
	if (state.always_ask || state.no_var_only)
		state.var_only = false;
}

void options_toggle(OptionsState& state, OptionChoice what)
{
	// Взаимоисключающие пары: включаем одно — снимаем другое. Так пункты
	// «Только интерактивные замены», «Всегда подтверждать замену» и «Только
	// бесспорные замены» не могут быть включены одновременно.
	switch (what)
	{
	case OptionChoice::var_only:
		state.var_only = !state.var_only;
		if (state.var_only)
		{
			state.no_var_only = false;
			state.always_ask = false;
		}
		return;
	case OptionChoice::always_ask:
		state.always_ask = !state.always_ask;
		if (state.always_ask)
			state.var_only = false;
		return;
	case OptionChoice::no_var_only:
		state.no_var_only = !state.no_var_only;
		if (state.no_var_only)
			state.var_only = false;
		return;
	default:
		break;
	}

	const auto flip = [](bool& flag) { flag = !flag; };
	switch (what)
	{
	case OptionChoice::check_yo:
		flip(state.check_yo);
		break;
	case OptionChoice::to_confirm_abbr:
		flip(state.to_confirm_abbr);
		break;
	case OptionChoice::to_confirm_cap:
		flip(state.to_confirm_cap);
		break;
	case OptionChoice::to_confirm_ellipsis:
		flip(state.to_confirm_ellipsis);
		break;
	case OptionChoice::propose_last:
		flip(state.propose_last);
		break;
	case OptionChoice::regexs:
		flip(state.regexs);
		break;
	case OptionChoice::fb_format:
		flip(state.fb_format);
		break;
	case OptionChoice::fast_scroll:
		flip(state.fast_scroll);
		break;
	case OptionChoice::mark:
		flip(state.mark);
		break;
	case OptionChoice::word_wrap:
		flip(state.word_wrap);
		break;
	case OptionChoice::recall_last_file:
		flip(state.recall_last_file);
		break;
	case OptionChoice::auto_unicode:
		flip(state.auto_unicode);
		break;
	case OptionChoice::show_toolbar:
		flip(state.show_toolbar);
		break;
	case OptionChoice::to_confirm_close:
		flip(state.to_confirm_close);
		break;
	case OptionChoice::silent_yofy:
		flip(state.silent_yofy);
		break;
	default:
		break; // вкладки, числа, цвета и пути переключаются иначе
	}
}

std::string options_font_text(float size) { return trim_size(size); }

std::string options_font_label(const OptionsState& state)
{
	if (state.font_name.empty())
		return trim_size(state.font_size);
	return state.font_name + ", " + trim_size(state.font_size);
}

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
		row.enabled = option_enabled(state, d.what);
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
