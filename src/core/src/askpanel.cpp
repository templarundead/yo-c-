#include "yo/askpanel.hpp"

#include "yo/codec.hpp"

#include <algorithm>
#include <cmath>
#include <utility>

namespace yo {

namespace {

// Убрать последний кодпоинт UTF-8.
void pop_back_cp(std::string& s)
{
	while (!s.empty())
	{
		const auto byte = static_cast<unsigned char>(s.back());
		s.pop_back();
		if ((byte & 0xC0U) != 0x80U)
			break;
	}
}

} // namespace

AskTexts make_ask_texts(const AskRequest& req)
{
	AskTexts texts;
	const std::string word1 = encode_line(req.word1, Encoding::utf8);
	const std::string word2 = encode_line(req.word2, Encoding::utf8);

	texts.variants = "1  Е: " + (word1.empty() ? std::string("—") : word1);
	if (!word2.empty())
		texts.variants += "          2  Ё: " + word2;

	if (req.wrong_yo)
		texts.reason = "вероятно, здесь должна быть Е — Ё лишняя";
	else if (req.replaced_yo)
		texts.reason = "странное расположение Ё";
	else if (req.wrong_ye)
		texts.reason = "вероятно, здесь должна быть Ё";
	else
		texts.reason = "выберите написание";
	return texts;
}

AskAnswer to_answer(AskChoice choice) noexcept
{
	switch (choice)
	{
	case AskChoice::ye:
		return AskAnswer::ye;
	case AskChoice::yo:
		return AskAnswer::yo;
	case AskChoice::ye_all:
		return AskAnswer::ye_all;
	case AskChoice::yo_all:
		return AskAnswer::yo_all;
	case AskChoice::skip:
		return AskAnswer::skip;
	case AskChoice::undo:
		return AskAnswer::undo;
	case AskChoice::cancel:
		break;
	}
	return AskAnswer::cancel;
}

int AskLayout::index_of(AskChoice choice) const noexcept
{
	for (std::size_t i = 0; i < buttons.size(); ++i)
	{
		if (buttons[i].choice == choice)
			return static_cast<int>(i);
	}
	return -1;
}

AskLayout layout_ask_panel(const AskLayoutInput& in, const AskMeasureFn& measure)
{
	AskLayout out;
	const float pad = std::max(2.0F, in.padding);
	const float gap = std::max(2.0F, pad * 0.5F);
	const float lh = std::max(1.0F, in.line_height);
	const float cw = std::max(1.0F, in.char_width);
	const float client_w = std::max(1.0F, in.client_w);
	const float client_h = std::max(1.0F, in.client_h);
	const float max_chars = std::max(8.0F, in.max_chars);

	const auto width = [&](std::string_view text) {
		if (measure)
			return measure(text);
		return static_cast<float>(text.size()) * cw;
	};

	// Панель не шире окна: длинные строки подрезаются с многоточием.
	// max_inner — та же ширина, что будет у панели внутри рамки.
	const float max_inner =
		std::max(24.0F * cw, std::min(client_w - 2.0F * pad, max_chars * cw) - 2.0F * pad);
	const auto clip = [&](std::string text) {
		if (width(text) <= max_inner)
			return text;
		const float whole = std::max(1.0F, width(text));
		const auto limit = static_cast<std::size_t>(
			static_cast<float>(text.size()) * (max_inner / whole));
		// Режем по границам кодпоинтов: незавершённый символ в UTF-8 недопустим.
		std::string cut;
		cut.reserve(std::min(text.size(), std::max<std::size_t>(1, limit)));
		for (std::size_t i = 0; i < text.size();)
		{
			const auto lead = static_cast<unsigned char>(text[i]);
			std::size_t len = lead < 0x80U	 ? 1
							  : (lead & 0xE0U) == 0xC0U ? 2
							  : (lead & 0xF0U) == 0xE0U ? 3
							  : (lead & 0xF8U) == 0xF0U ? 4
														: 1;
			if (i + len > text.size())
				len = 1;
			if (cut.size() + len > limit)
				break;
			cut.append(text, i, len);
			i += len;
		}
		while (!cut.empty() && width(cut) > max_inner)
			pop_back_cp(cut);
		return cut + "…";
	};

	std::vector<std::string> texts = { clip(in.variants), clip(in.reason), clip(in.context) };

	struct Entry
	{
		AskChoice choice = AskChoice::cancel;
		std::string label;
		float width = 0.0F;
	};
	std::vector<Entry> entries = {
		{ AskChoice::ye, "1  Е", 0.0F },
		{ AskChoice::yo, "2  Ё", 0.0F },
		{ AskChoice::ye_all, "3  Всегда Е", 0.0F },
		{ AskChoice::yo_all, "4  Всегда Ё", 0.0F },
		{ AskChoice::skip, "5  Пропустить", 0.0F },
		{ AskChoice::undo, "6  Откат", 0.0F },
		{ AskChoice::cancel, "Esc  Прервать", 0.0F },
	};
	for (Entry& e : entries)
		e.width = std::max(7.0F * cw, width(e.label) + 2.0F * gap);

	// Кнопки в один ряд, а если не влезают — в несколько.
	std::vector<std::vector<std::size_t>> lines;
	std::vector<float> line_w;
	float used = 0.0F;
	for (std::size_t i = 0; i < entries.size(); ++i)
	{
		const float w = entries[i].width;
		if (lines.empty() || used + gap + w > max_inner)
		{
			lines.emplace_back();
			line_w.push_back(w);
			used = w;
		}
		else
		{
			used += gap + w;
			line_w.back() = used;
		}
		lines.back().push_back(i);
	}

	float inner_w = 0.0F;
	for (const float w : line_w)
		inner_w = std::max(inner_w, w);
	for (const std::string& text : texts)
		inner_w = std::max(inner_w, width(text));

	const float panel_w = std::min(client_w - 2.0F * pad, inner_w + 2.0F * pad);
	const float button_h = lh + 2.0F * gap;
	const float panel_h =
		pad + 3.0F * lh + gap + static_cast<float>(lines.size()) * (button_h + gap) + pad;

	// Панель прижата к низу окна над строкой состояния.
	const float bottom = std::max(client_h - in.status_h - pad, panel_h + pad);
	const float left = std::max(pad, (client_w - panel_w) * 0.5F);

	out.visible = true;
	out.x = left;
	out.y = bottom - panel_h;
	out.w = panel_w;
	out.h = panel_h;

	const float text_left = out.x + pad;
	float y = out.y + pad;
	for (const std::string& text : texts)
	{
		AskLayout::Row row;
		row.text = text;
		row.y = y;
		row.h = lh;
		out.rows.push_back(std::move(row));
		y += lh;
	}
	y += gap;

	for (const std::vector<std::size_t>& line : lines)
	{
		float x = text_left;
		for (const std::size_t index : line)
		{
			AskButtonBox box;
			box.choice = entries[index].choice;
			box.label = entries[index].label;
			box.x = x;
			box.y = y;
			// Совсем узкое окно: кнопка не шире панели, подпись подрежется.
			box.w = std::min(entries[index].width, max_inner);
			box.h = button_h;
			out.buttons.push_back(std::move(box));
			x += entries[index].width + gap;
		}
		y += button_h + gap;
	}
	return out;
}

} // namespace yo
