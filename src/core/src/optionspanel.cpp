#include "yo/optionspanel.hpp"

#include <algorithm>
#include <cmath>

namespace yo {

namespace {

constexpr float kPad = 14.0F;		// поля окна
constexpr float kRowGap = 2.0F;		// зазор между строками
constexpr float kPanelMinW = 380.0F;

// Приблизительная ширина строки: точные метрики есть только у GUI, а для
// раскладки достаточно пропорции — тесты проверяют, что кнопки не
// наезжают друг на друга и лежат внутри окна.
float text_width(std::string_view text, float line_h)
{
	return static_cast<float>(text.size()) * line_h * 0.52F;
}

std::string trim_size(float size)
{
	const int rounded = static_cast<int>(std::lround(size));
	return std::to_string(std::max(1, rounded));
}

} // namespace

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

	struct Definition
	{
		OptionChoice what;
		std::string label;
		bool check;
		bool checked;
		bool path;
		std::string value;
	};
	const std::vector<Definition> defs = {
		{ OptionChoice::always_ask, "Спрашивать про все слова", true, state.always_ask, false, {} },
		{ OptionChoice::silent_yofy, "Ёфицировать без вопросов", true, state.silent_yofy, false, {} },
		{ OptionChoice::mark, "Помечать замены", true, state.mark, false, {} },
		{ OptionChoice::propose_last, "Предлагать последний вариант", true, state.propose_last, false, {} },
		{ OptionChoice::regexs, "Использовать словарь РВ", true, state.regexs, false, {} },
		{ OptionChoice::to_confirm_abbr, "Спрашивать про сокращения", true, state.to_confirm_abbr, false,
		  {} },
		{ OptionChoice::dictionary, "Основной словарь", false, false, true, state.dictionary },
		{ OptionChoice::regex_dictionary, "Словарь РВ", false, false, true, state.regex_dictionary },
	};

	const float font_row_h = row_h;

	// Ширина окна: самая длинная строка плюс место на значение справа.
	float content = text_width(out.title, lh) * 1.4F;
	for (const Definition& d : defs)
		content = std::max(content, text_width(d.label, lh) + text_width(d.value, lh) + 160.0F);
	content = std::max(content, text_width("Размер текста", lh) + 140.0F);

	out.w = std::clamp(content + 2.0F * kPad, std::min(kPanelMinW, width - 16.0F), width - 16.0F);
	out.w = std::max(out.w, 200.0F);

	const float rows_h = static_cast<float>(defs.size()) * (row_h + kRowGap) + font_row_h + kRowGap;
	out.h = kPad + lh * 1.8F + rows_h + kPad + button_h + kPad;
	out.h = std::min(out.h, std::max(120.0F, height - 16.0F));
	out.x = std::max(8.0F, (width - out.w) * 0.5F);
	out.y = std::max(8.0F, (height - out.h) * 0.5F);
	out.visible = true;

	float y = out.y + kPad + lh * 1.8F;
	for (const Definition& d : defs)
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

	// Строка размера шрифта: подпись, значение и кнопки «−»/«+» справа.
	OptionRowBox font;
	font.what = OptionChoice::font_smaller; // клик по строке ничего не меняет
	font.label = "Размер текста";
	font.value = trim_size(state.font_size);
	font.x = out.x + kPad;
	font.y = y;
	font.w = out.w - 2.0F * kPad;
	font.h = font_row_h;
	out.rows.push_back(std::move(font));
	y += font_row_h + kRowGap;

	const float small = std::max(22.0F, lh + 10.0F);
	const float row_right = out.x + out.w - kPad;
	const float buttons_row_y = font.y + (font_row_h - small) * 0.5F;
	out.buttons.push_back(OptionButtonBox{ OptionChoice::font_bigger, "+", row_right - small,
										   buttons_row_y, small, small });
	out.buttons.push_back(OptionButtonBox{ OptionChoice::font_smaller, "−", row_right - 2.0F * small - 4.0F,
										   buttons_row_y, small, small });

	// Кнопки внизу: «Применить», «Запомнить», «Отмена» — по ширине подписи.
	const std::vector<std::pair<OptionChoice, std::string>> actions = {
		{ OptionChoice::apply, "Применить" },
		{ OptionChoice::apply_remember, "Запомнить" },
		{ OptionChoice::cancel, "Отмена" },
	};
	float total = 0.0F;
	for (const auto& [what, label] : actions)
		total += text_width(label, lh) + 28.0F;
	const float gap = 8.0F;
	total += gap * static_cast<float>(actions.size() - 1);
	const float scale = total > out.w - 2.0F * kPad ? (out.w - 2.0F * kPad) / total : 1.0F;

	float bx = out.x + out.w - kPad; // справа налево
	const float by = out.y + out.h - kPad - button_h;
	for (auto it = actions.rbegin(); it != actions.rend(); ++it)
	{
		const float bw = (text_width(it->second, lh) + 28.0F) * scale;
		out.buttons.push_back(OptionButtonBox{ it->first, it->second, bx - bw, by, bw, button_h });
		bx -= bw + gap * scale;
	}
	return out;
}

} // namespace yo
