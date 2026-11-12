#include "yo/editor.hpp"

#include <algorithm>
#include <cmath>

namespace yo {

namespace {

[[nodiscard]] bool is_cont(unsigned char c) noexcept { return (c & 0xC0U) == 0x80U; }

// Начало символа, заканчивающегося в pos (pos > limit).
[[nodiscard]] std::size_t prev_cp(std::string_view s, std::size_t pos) noexcept
{
	if (pos == 0)
		return 0;
	std::size_t p = pos - 1;
	std::size_t guard = 0;
	while (p > 0 && is_cont(static_cast<unsigned char>(s[p])) && guard < 3)
	{
		--p;
		++guard;
	}
	return p;
}

// Конец символа, начинающегося в pos (pos < s.size()).
[[nodiscard]] std::size_t next_cp(std::string_view s, std::size_t pos) noexcept
{
	if (pos >= s.size())
		return s.size();
	std::size_t p = pos + 1;
	std::size_t guard = 0;
	while (p < s.size() && is_cont(static_cast<unsigned char>(s[p])) && guard < 3)
	{
		++p;
		++guard;
	}
	return p;
}

std::string encode_utf8(char32_t cp)
{
	std::string out;
	if (cp < 0x80)
		out.push_back(static_cast<char>(cp));
	else if (cp < 0x800)
	{
		out.push_back(static_cast<char>(0xC0U | (cp >> 6)));
		out.push_back(static_cast<char>(0x80U | (cp & 0x3FU)));
	}
	else if (cp < 0x10000)
	{
		out.push_back(static_cast<char>(0xE0U | (cp >> 12)));
		out.push_back(static_cast<char>(0x80U | ((cp >> 6) & 0x3FU)));
		out.push_back(static_cast<char>(0x80U | (cp & 0x3FU)));
	}
	else
	{
		out.push_back(static_cast<char>(0xF0U | (cp >> 18)));
		out.push_back(static_cast<char>(0x80U | ((cp >> 12) & 0x3FU)));
		out.push_back(static_cast<char>(0x80U | ((cp >> 6) & 0x3FU)));
		out.push_back(static_cast<char>(0x80U | (cp & 0x3FU)));
	}
	return out;
}

[[nodiscard]] bool is_space(unsigned char c) noexcept
{
	return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\v' || c == '\f';
}

} // namespace

Editor::Editor(std::shared_ptr<TextBuffer> buffer) : buf_(std::move(buffer))
{
	if (!buf_)
		buf_ = std::make_shared<TextBuffer>();
}

void Editor::set_metrics(const Metrics& m) noexcept
{
	metrics_ = m;
	width_dirty_ = true;
	// Геометрия изменилась — подрезаем прокрутку под новый размер строки, но
	// курсор в окно НЕ тянем: рендерер вызывает set_metrics на каждой
	// перерисовке, и прежнее поведение отменяло прокрутку колесом и полосой.
	scroll_y_ = std::min(scroll_y_, std::max(0.0F, content_height() - view_h_));
	scroll_x_ = std::max(0.0F, scroll_x_);
}

std::size_t Editor::clamp_offset(std::size_t offset) const
{
	return std::min(offset, buf_->size());
}

std::size_t Editor::sel_begin() const noexcept { return std::min(caret_, anchor_); }
std::size_t Editor::sel_end() const noexcept { return std::max(caret_, anchor_); }

std::string Editor::selected_text() const
{
	const std::size_t a = sel_begin();
	const std::size_t b = sel_end();
	if (a == b)
		return {};
	std::string out;
	buf_->read(a, b - a, out);
	return out;
}

CaretPos Editor::caret_pos() const
{
	const std::size_t line = buf_->line_of(caret_);
	const std::size_t start = buf_->line_start(line);
	CaretPos p;
	p.line = line + 1;
	p.column = prefix_cells(line, caret_ - start) + 1;
	return p;
}

// ---- выделение и клики ----

void Editor::set_caret(std::size_t offset, bool extend)
{
	caret_ = clamp_offset(offset);
	if (!extend)
		anchor_ = caret_;
	want_cells_valid_ = false;
	ensure_caret_visible_impl();
}

void Editor::select_all()
{
	anchor_ = 0;
	caret_ = buf_->size();
	want_cells_valid_ = false;
	ensure_caret_visible_impl();
}

void Editor::click_at(float x, float y, bool extend)
{
	const std::size_t off = offset_at(x, y);
	caret_ = off;
	if (!extend)
		anchor_ = off;
	want_cells_valid_ = false;
	ensure_caret_visible_impl();
}

// ---- движение курсора ----

void Editor::move_left(bool extend, bool by_word)
{
	std::size_t pos = caret_;
	if (has_selection() && !extend)
	{
		caret_ = sel_begin();
		anchor_ = caret_;
	}
	else if (by_word)
	{
		// Пробелы назад, затем слово.
		std::string s;
		buf_->read(pos > 64 ? pos - 64 : 0, std::min<std::size_t>(64, pos), s);
		const std::size_t base = pos > 64 ? pos - 64 : 0;
		std::size_t p = pos - base;
		while (p > 0 && is_space(static_cast<unsigned char>(s[p - 1])))
			--p;
		while (p > 0 && !is_space(static_cast<unsigned char>(s[p - 1])))
			--p;
		pos = base + p;
		caret_ = pos;
		if (!extend)
			anchor_ = caret_;
	}
	else if (pos > 0)
	{
		std::string s;
		const std::size_t from = pos > 8 ? pos - 8 : 0;
		buf_->read(from, pos - from, s);
		caret_ = from + prev_cp(s, s.size());
		if (!extend)
			anchor_ = caret_;
	}
	else
	{
		caret_ = 0;
		if (!extend)
			anchor_ = 0;
	}
	want_cells_valid_ = false;
	ensure_caret_visible_impl();
}

void Editor::move_right(bool extend, bool by_word)
{
	std::size_t pos = caret_;
	if (has_selection() && !extend)
	{
		caret_ = sel_end();
		anchor_ = caret_;
	}
	else if (by_word)
	{
		std::string s;
		buf_->read(pos, std::min<std::size_t>(64, buf_->size() - pos), s);
		std::size_t p = 0;
		while (p < s.size() && is_space(static_cast<unsigned char>(s[p])))
			++p;
		while (p < s.size() && !is_space(static_cast<unsigned char>(s[p])))
			++p;
		caret_ = pos + p;
		if (!extend)
			anchor_ = caret_;
	}
	else if (pos < buf_->size())
	{
		std::string s;
		const std::size_t len = std::min<std::size_t>(8, buf_->size() - pos);
		buf_->read(pos, len, s);
		caret_ = pos + next_cp(s, 0);
		if (!extend)
			anchor_ = caret_;
	}
	else
	{
		caret_ = buf_->size();
		if (!extend)
			anchor_ = caret_;
	}
	want_cells_valid_ = false;
	ensure_caret_visible_impl();
}

void Editor::move_vertically(long lines, bool extend)
{
	const std::size_t line = buf_->line_of(caret_);
	const std::size_t byte_in_line = caret_ - buf_->line_start(line);

	if (!want_cells_valid_)
	{
		want_cells_ = prefix_cells(line, byte_in_line);
		want_cells_valid_ = true;
	}

	long target = static_cast<long>(line) + lines;
	if (target < 0)
		target = 0;
	if (static_cast<std::size_t>(target) >= buf_->line_count())
		target = static_cast<long>(buf_->line_count()) - 1;

	const std::size_t tline = static_cast<std::size_t>(target);
	caret_ = buf_->line_start(tline) + cells_to_byte(tline, want_cells_);
	if (!extend)
		anchor_ = caret_;
	ensure_caret_visible_impl();
}

void Editor::move_up(bool extend) { move_vertically(-1, extend); }
void Editor::move_down(bool extend) { move_vertically(1, extend); }

void Editor::move_page(long pages, bool extend)
{
	const long per_page = std::max<long>(1, static_cast<long>(view_h_ / metrics_.line_height));
	move_vertically(pages * per_page, extend);
}

void Editor::move_line_start(bool extend)
{
	const std::size_t line = buf_->line_of(caret_);
	caret_ = buf_->line_start(line);
	if (!extend)
		anchor_ = caret_;
	want_cells_valid_ = false;
	ensure_caret_visible_impl();
}

void Editor::move_line_end(bool extend)
{
	const std::size_t line = buf_->line_of(caret_);
	caret_ = buf_->line_start(line) + buf_->line_text(line).size();
	if (!extend)
		anchor_ = caret_;
	want_cells_valid_ = false;
	ensure_caret_visible_impl();
}

void Editor::move_doc_start(bool extend)
{
	caret_ = 0;
	if (!extend)
		anchor_ = 0;
	want_cells_valid_ = false;
	ensure_caret_visible_impl();
}

void Editor::move_doc_end(bool extend)
{
	caret_ = buf_->size();
	if (!extend)
		anchor_ = caret_;
	want_cells_valid_ = false;
	ensure_caret_visible_impl();
}

// ---- правки ----

void Editor::after_edit(std::size_t edited)
{
	anchor_ = caret_;
	want_cells_valid_ = false;
	if (!width_dirty_)
	{
		const std::size_t line = buf_->line_of(edited);
		const float w = static_cast<float>(prefix_cells(line, buf_->line_text(line).size())) *
						metrics_.char_width;
		if (w > cached_text_width_)
			cached_text_width_ = w;
	}
	ensure_caret_visible_impl();
}

bool Editor::delete_selection()
{
	const std::size_t a = sel_begin();
	const std::size_t b = sel_end();
	if (a == b)
		return false;
	buf_->erase(a, b - a);
	caret_ = a;
	after_edit(a);
	return true;
}

bool Editor::insert_text(std::string_view utf8)
{
	if (utf8.empty())
	{
		if (!has_selection())
			return false;
	}
	bool edited = delete_selection();
	const std::size_t at = caret_;
	if (!utf8.empty())
	{
		buf_->insert(at, utf8);
		caret_ = at + utf8.size();
		edited = true;
	}
	if (edited)
		after_edit(at);
	return edited;
}

bool Editor::insert_char(char32_t cp) { return insert_text(encode_utf8(cp)); }

bool Editor::backspace()
{
	if (delete_selection())
		return true;
	const std::size_t pos = caret_;
	if (pos == 0)
		return false;
	std::size_t prev = pos - 1;
	// Не разрываем UTF-8: отступаем к началу символа.
	{
		const std::size_t from = pos > 8 ? pos - 8 : 0;
		std::string s;
		buf_->read(from, pos - from, s);
		prev = from + prev_cp(s, s.size());
	}
	buf_->erase(prev, pos - prev);
	caret_ = prev;
	after_edit(prev);
	return true;
}

bool Editor::delete_forward()
{
	if (delete_selection())
		return true;
	const std::size_t pos = caret_;
	if (pos >= buf_->size())
		return false;
	std::string s;
	const std::size_t len = std::min<std::size_t>(8, buf_->size() - pos);
	buf_->read(pos, len, s);
	const std::size_t next = pos + next_cp(s, 0);
	buf_->erase(pos, next - pos);
	caret_ = pos;
	after_edit(pos);
	return true;
}

bool Editor::cut_selection(std::string* out)
{
	const std::size_t a = sel_begin();
	const std::size_t b = sel_end();
	if (a == b)
		return false;
	if (out != nullptr)
		buf_->read(a, b - a, *out);
	buf_->erase(a, b - a);
	caret_ = a;
	after_edit(a);
	return true;
}

bool Editor::copy_selection(std::string* out) const
{
	const std::size_t a = sel_begin();
	const std::size_t b = sel_end();
	if (a == b || out == nullptr)
		return false;
	buf_->read(a, b - a, *out);
	return true;
}

bool Editor::paste(std::string_view utf8) { return insert_text(utf8); }

// ---- геометрия ----

std::size_t Editor::prefix_cells(std::size_t line, std::size_t byte_in_line) const
{
	const std::string_view text = buf_->line_text(line);
	std::size_t cells = 0;
	const std::size_t limit = std::min(byte_in_line, text.size());
	for (std::size_t i = 0; i < limit; ++i)
	{
		const unsigned char c = static_cast<unsigned char>(text[i]);
		if (is_cont(c))
			continue;
		if (c == '\t')
		{
			const std::size_t tab = static_cast<std::size_t>(std::max(1.0F, metrics_.tab_cells));
			cells += tab - (cells % tab);
			continue;
		}
		++cells;
	}
	return cells;
}

float Editor::prefix_width(std::size_t line, std::size_t byte_in_line) const
{
	return static_cast<float>(prefix_cells(line, byte_in_line)) * metrics_.char_width;
}

std::size_t Editor::cells_to_byte(std::size_t line, std::size_t cells) const
{
	const std::string_view text = buf_->line_text(line);
	std::size_t cur = 0;
	std::size_t i = 0;
	while (i < text.size())
	{
		if (cur >= cells)
			return i;
		const unsigned char c = static_cast<unsigned char>(text[i]);
		const std::size_t step = next_cp(text, i) - i;
		if (c == '\t')
		{
			const std::size_t tab = static_cast<std::size_t>(std::max(1.0F, metrics_.tab_cells));
			const std::size_t next_stop = cur + tab - (cur % tab);
			if (next_stop > cells)
				return i; // клик внутри табуляции — на её начало
			cur = next_stop;
		}
		else
		{
			++cur;
		}
		i += step;
	}
	return text.size();
}

float Editor::caret_x() const
{
	const std::size_t line = buf_->line_of(caret_);
	return prefix_width(line, caret_ - buf_->line_start(line));
}

float Editor::caret_y() const { return line_top(buf_->line_of(caret_)); }

float Editor::line_top(std::size_t line) const
{
	return static_cast<float>(line) * metrics_.line_height;
}

std::size_t Editor::offset_at(float x, float y) const
{
	if (buf_->line_count() == 0)
		return 0;
	const float lh = std::max(1.0F, metrics_.line_height);
	long line = static_cast<long>(y / lh);
	if (line < 0)
		line = 0;
	if (static_cast<std::size_t>(line) >= buf_->line_count())
		line = static_cast<long>(buf_->line_count()) - 1;
	const std::size_t l = static_cast<std::size_t>(line);

	const float cw = std::max(1.0F, metrics_.char_width);
	const long cells = static_cast<long>(x / cw + 0.5F);
	const std::size_t want = cells > 0 ? static_cast<std::size_t>(cells) : 0;
	return buf_->line_start(l) + cells_to_byte(l, want);
}

// ---- вид и прокрутка ----

void Editor::set_viewport(float width, float height)
{
	view_w_ = std::max(0.0F, width);
	view_h_ = std::max(0.0F, height);
	// Только подрезка прокрутки под новый размер окна (см. set_metrics):
	// возврат к курсору здесь отменял бы прокрутку колесом и полосой.
	scroll_y_ = std::min(scroll_y_, std::max(0.0F, content_height() - view_h_));
	const float max_x = std::max(0.0F, text_width() - view_w_ + metrics_.char_width);
	scroll_x_ = std::min(scroll_x_, std::max(0.0F, max_x));
}

std::size_t Editor::clamp_scroll_y(float y) const
{
	const float max_y = std::max(0.0F, content_height() - view_h_);
	if (y <= 0.0F)
		return 0;
	if (y >= max_y)
		return static_cast<std::size_t>(max_y);
	return static_cast<std::size_t>(y);
}

void Editor::scroll_by(float dx, float dy)
{
	const float max_x = std::max(0.0F, text_width() - view_w_ + metrics_.char_width);
	scroll_x_ = std::max(0.0F, std::min(scroll_x_ + dx, max_x));
	const float max_y = std::max(0.0F, content_height() - view_h_);
	scroll_y_ = std::max(0.0F, std::min(scroll_y_ + dy, max_y));
}

void Editor::scroll_lines(long lines)
{
	scroll_by(0.0F, static_cast<float>(lines) * metrics_.line_height);
}

void Editor::ensure_caret_visible_impl()
{
	const float lh = std::max(1.0F, metrics_.line_height);
	const float top = caret_y();
	const float bottom = top + lh;
	if (top < scroll_y_)
		scroll_y_ = top;
	else if (bottom > scroll_y_ + view_h_)
		scroll_y_ = bottom - view_h_;
	scroll_y_ = std::max(0.0F, std::min(scroll_y_, std::max(0.0F, content_height() - view_h_)));

	const float cx = caret_x();
	const float cw = std::max(1.0F, metrics_.char_width);
	const float right = scroll_x_ + view_w_;
	if (cx < scroll_x_)
		scroll_x_ = cx;
	else if (cx + cw > right)
		scroll_x_ = cx + cw - view_w_;
	scroll_x_ = std::max(0.0F, scroll_x_);
}

void Editor::ensure_caret_visible() { ensure_caret_visible_impl(); }

std::size_t Editor::first_visible_line() const
{
	const float lh = std::max(1.0F, metrics_.line_height);
	const std::size_t line = static_cast<std::size_t>(scroll_y_ / lh);
	return std::min(line, buf_->line_count() - 1);
}

std::size_t Editor::visible_line_count() const
{
	const float lh = std::max(1.0F, metrics_.line_height);
	const std::size_t lines = static_cast<std::size_t>(std::ceil(view_h_ / lh)) + 1;
	return std::min(lines, buf_->line_count() - first_visible_line());
}

float Editor::content_height() const
{
	return static_cast<float>(buf_->line_count()) * std::max(1.0F, metrics_.line_height);
}

namespace {
// Сколько строк просматривать при первом вычислении ширины текста.
constexpr std::size_t kWidthScanLines = 5000;
} // namespace

float Editor::text_width() const
{
	if (width_dirty_)
	{
		float best = 0.0F;
		const std::size_t n = std::min<std::size_t>(buf_->line_count(), kWidthScanLines);
		for (std::size_t i = 0; i < n; ++i)
		{
			const std::size_t cells = prefix_cells(i, buf_->line_text(i).size());
			const float w = static_cast<float>(cells) * metrics_.char_width;
			if (w > best)
				best = w;
		}
		cached_text_width_ = best;
		width_dirty_ = false;
	}
	return cached_text_width_;
}

void Editor::note_line_cells(std::size_t cells) const
{
	const float w = static_cast<float>(cells) * metrics_.char_width;
	if (w > cached_text_width_)
		cached_text_width_ = w;
}

void Editor::invalidate_width() { width_dirty_ = true; }

} // namespace yo
