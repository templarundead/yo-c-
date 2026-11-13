#include "yo/askpanel.hpp"

#include "yo/codec.hpp"

#include <algorithm>
#include <cmath>
#include <utility>

namespace yo {

AskWordRange ask_word_bytes(std::string_view line_utf8, std::size_t offset_cp, std::size_t length_cp)
{
	// Кодовые точки считаем тем же декодером, что и остальной текст, поэтому
	// суррогаты/невалидные байты дают тот же результат, что и при чтении строки.
	const auto bytes = std::span<const std::uint8_t>(
		reinterpret_cast<const std::uint8_t*>(line_utf8.data()), line_utf8.size());
	const UString u32 = decode_line(bytes, Encoding::utf8);
	const std::size_t from32 = std::min(offset_cp, u32.size());
	const std::size_t to32 = std::min(from32 + length_cp, u32.size());
	const std::size_t from = encode_line(UString(u32.substr(0, from32)), Encoding::utf8).size();
	const std::size_t size =
		encode_line(UString(u32.substr(from32, to32 - from32)), Encoding::utf8).size();
	return AskWordRange{ from, from + size };
}

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

// Подрезать строку по ширине с многоточием, не разрывая символы UTF-8.
std::string clip_to_width(std::string text, float max_w, const AskMeasureFn& measure, float cw)
{
	const auto width = [&](std::string_view t) {
		if (measure)
			return measure(t);
		return static_cast<float>(t.size()) * cw;
	};
	if (width(text) <= max_w)
		return text;
	const float whole = std::max(1.0F, width(text));
	const auto limit =
		static_cast<std::size_t>(static_cast<float>(text.size()) * (max_w / whole));
	std::string cut;
	cut.reserve(std::min(text.size(), std::max<std::size_t>(1, limit)));
	for (std::size_t i = 0; i < text.size();)
	{
		const auto lead = static_cast<unsigned char>(text[i]);
		std::size_t len = lead < 0x80U ? 1
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
	while (!cut.empty() && width(cut) > max_w)
		pop_back_cp(cut);
	return cut + "…";
}

// Панель как в оригинале: полоса во всю ширину над строкой состояния,
// заголовок по центру, кнопки в две колонки с вариантами слова.
AskLayout layout_bottom_bar(const AskLayoutInput& in, const AskMeasureFn& measure)
{
	AskLayout out;
	const float pad = std::max(4.0F, in.padding);
	const float gap = std::max(3.0F, pad * 0.5F);
	const float lh = std::max(1.0F, in.line_height);
	const float cw = std::max(1.0F, in.char_width);
	const float client_w = std::max(1.0F, in.client_w);
	const float client_h = std::max(1.0F, in.client_h);
	const float avail_w = std::max(40.0F, client_w - 2.0F * pad);

	const auto width = [&](std::string_view text) {
		if (measure)
			return measure(text);
		return static_cast<float>(text.size()) * cw;
	};

	struct Entry
	{
		AskChoice choice = AskChoice::cancel;
		std::string label;
	};

	const std::string& w1 = in.word1;
	const std::string& w2 = in.word2;
	const auto key = [](std::string text, const char* k)
	{
		text += " [";
		text += k;
		text += "]";
		return text;
	};

	// Две колонки: слева выбор написания, справа «всегда»/«пропустить».
	// «Прервать» — отдельной кнопкой во всю ширину этих двух колонок.
	std::vector<Entry> left;
	left.push_back({ AskChoice::ye, key(w1.empty() ? std::string("Е") : w1, "1") });
	left.push_back({ AskChoice::yo, key(w2.empty() ? std::string("Ё") : w2, "2") });
	left.push_back({ AskChoice::undo, key(std::string("Откат"), "6") });

	std::vector<Entry> right;
	right.push_back({ AskChoice::ye_all, key(std::string("Всегда Е"), "3") });
	right.push_back({ AskChoice::yo_all, key(std::string("Всегда Ё"), "4") });
	right.push_back({ AskChoice::skip, key(std::string("Пропустить"), "5") });
	const Entry cancel_entry{ AskChoice::cancel, key(std::string("Прервать"), "Esc") };

	// Ширина колонки: самая длинная подпись плюс поля по краям. Подпись
	// рисуется тем же шрифтом, которым её измерили (measure — шрифт панели),
	// иначе текст обрезался с краю («1ропустить [5]»).
	const auto column_w = [&](const std::vector<Entry>& column)
	{
		float w = 0.0F;
		for (const Entry& e : column)
			w = std::max(w, width(e.label));
		w = std::max(w, width(cancel_entry.label) * 0.5F);
		return std::min(avail_w, std::max(12.0F * cw, w + 4.0F * gap));
	};

	// Две колонки; не влезают — одна колонка (кнопки всё равно нажимаются).
	const bool two_columns = column_w(left) + 2.0F * gap + column_w(right) <= avail_w;
	float col_w = two_columns ? column_w(left) : 0.0F;
	float col_w2 = two_columns ? column_w(right) : 0.0F;
	std::vector<Entry> single;
	if (!two_columns)
	{
		single = left;
		single.push_back(cancel_entry);
		single.push_back(right[0]);
		single.push_back(right[1]);
		single.push_back(right[2]);
		float w = 0.0F;
		for (const Entry& e : single)
			w = std::max(w, width(e.label));
		col_w = std::min(avail_w, std::max(12.0F * cw, w + 4.0F * gap));
	}

	// Строки текста: сначала предложение с обрабатываемым словом, затем
	// заголовок — они идут над кнопками, а не над всем блоком.
	std::string title = clip_to_width(in.reason, avail_w, measure, cw);
	// Контекст не переносим и не режем: слово должно стоять по центру полосы,
	// а края предложения подрежет рисование.
	std::vector<std::string> context;
	if (!in.context.empty())
		context.push_back(in.context);

	const float rows_h = two_columns ? 3.0F : static_cast<float>(single.size()) * 1.0F;
	const float button_h = lh + 2.0F * gap;
	float h = pad;
	h += static_cast<float>(context.size()) * lh;
	h += gap;
	if (!title.empty())
		h += lh + gap;
	h += rows_h * (button_h + gap);
	h += button_h + gap; // «Прервать» во всю ширину
	h += pad - gap;

	// Если полоса не помещается в окно, честнее нарисовать компактную
	// плавающую панель: её App покажет по центру.
	if (h > client_h - in.status_h - pad * 2.0F)
		return out;

	out.visible = true;
	out.bottom = true;
	out.x = 0.0F;
	out.w = client_w;
	out.h = h;
	out.y = std::max(0.0F, client_h - in.status_h - h);

	float y = out.y + pad;
	for (const std::string& text : context)
	{
		AskLayout::Row row;
		row.text = text;
		row.y = y;
		row.h = lh;
		row.kind = AskRowKind::context;
		out.rows.push_back(std::move(row));
		y += lh;
	}
	y += gap;
	if (!title.empty())
	{
		AskLayout::Row row;
		row.text = title;
		row.y = y;
		row.h = lh;
		row.kind = AskRowKind::title;
		out.rows.push_back(std::move(row));
		y += lh + gap;
	}

	const auto add_button = [&](AskChoice choice, const std::string& label, float x, float y0, float w)
	{
		AskButtonBox box;
		box.choice = choice;
		box.label = label;
		box.x = x;
		box.y = y0;
		box.w = w;
		box.h = button_h;
		out.buttons.push_back(std::move(box));
	};

	if (two_columns)
	{
		const float total = col_w + 2.0F * gap + col_w2;
		const float x0 = std::max(pad, (client_w - total) * 0.5F);
		const float x1 = x0 + col_w + 2.0F * gap;
		for (std::size_t k = 0; k < left.size(); ++k)
			add_button(left[k].choice, left[k].label, x0, y + static_cast<float>(k) * (button_h + gap), col_w);
		for (std::size_t k = 0; k < right.size(); ++k)
			add_button(right[k].choice, right[k].label, x1, y + static_cast<float>(k) * (button_h + gap), col_w2);

		// «Прервать» — по ширине двух верхних кнопок.
		const float cancel_y = y + static_cast<float>(left.size()) * (button_h + gap);
		add_button(cancel_entry.choice, cancel_entry.label, x0, cancel_y, total);
	}
	else
	{
		const float x0 = std::max(pad, (client_w - col_w) * 0.5F);
		for (std::size_t k = 0; k < single.size(); ++k)
			add_button(single[k].choice, single[k].label, x0, y + static_cast<float>(k) * (button_h + gap), col_w);
	}
	return out;
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
		texts.reason = "Выберите верный вариант";
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
	if (in.bottom_bar)
	{
		AskLayout bottom = layout_bottom_bar(in, measure);
		if (bottom.visible)
			return bottom;
	}

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
		return clip_to_width(std::move(text), max_inner, measure, cw);
	};

	// Контекст — предложение с обрабатываемым словом; он может занимать
	// несколько строк (App переносит его по ширине панели).
	std::vector<std::string> texts = { clip(in.variants), clip(in.reason) };
	{
		std::size_t begin = 0;
		const std::string& ctx = in.context;
		while (begin <= ctx.size())
		{
			const std::size_t end = ctx.find('\n', begin);
			const std::string part =
				end == std::string::npos ? ctx.substr(begin) : ctx.substr(begin, end - begin);
			texts.push_back(clip(part));
			if (end == std::string::npos)
				break;
			begin = end + 1;
			if (texts.size() > 24)
				break; // предохранитель: панель не должна занимать весь экран
		}
	}
	if (in.context.empty())
		texts.pop_back();

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
		e.width = std::max(9.0F * cw, width(e.label) + 3.0F * gap);

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
	const float button_h = lh + 3.0F * gap;
	const float panel_h = pad + static_cast<float>(texts.size()) * lh + gap +
						  static_cast<float>(lines.size()) * (button_h + gap) + pad;

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
	for (std::size_t i = 0; i < texts.size(); ++i)
	{
		AskLayout::Row row;
		row.text = texts[i];
		row.y = y;
		row.h = lh;
		// Первая строка — варианты слова, вторая — причина/заголовок,
		// остальные — предложение с обрабатываемым словом.
		row.kind = i == 0 ? AskRowKind::variant : (i == 1 ? AskRowKind::title : AskRowKind::context);
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
