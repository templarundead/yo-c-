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

// Строка длиннее этого числа «клеток» не переносится: одна такая строка
// (минифицированный файл) иначе заставляла бы пересчитывать десятки тысяч
// рядов на каждую перерисовку.
constexpr std::size_t kWrapGuardCells = 20000;

// Предел числа строк документа для переноса: раскладка рядов пересчитывается
// от изменённой строки до конца файла, поэтому на очень больших файлах
// перенос выключается сам (иначе каждая правка — проход по всему тексту).
constexpr std::size_t kWrapMaxLines = 50000;

} // namespace

Editor::Editor(std::shared_ptr<TextBuffer> buffer) : buf_(std::move(buffer))
{
	if (!buf_)
		buf_ = std::make_shared<TextBuffer>();
}

void Editor::set_metrics(const Metrics& m) noexcept
{
	// Рендерер сообщает метрики на каждой перерисовке; пересчёт ширины
	// (проход по видимым строкам) нужен только при их реальном изменении.
	const bool changed = m.char_width != metrics_.char_width || m.line_height != metrics_.line_height ||
						 m.tab_cells != metrics_.tab_cells || m.padding_x != metrics_.padding_x;
	metrics_ = m;
	if (changed)
	{
		width_dirty_ = true;
		invalidate_rows(0);
		if (wrap_)
			wrap_cells_ = std::max<std::size_t>(
				8, static_cast<std::size_t>(std::max(0.0F, view_w_) /
											std::max(1.0F, metrics_.char_width)));
	}
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

	if (wrap_)
	{
		// При переносе «вверх/вниз» — по визуальным рядам, как в редакторах.
		const std::size_t row = row_of_offset(caret_);
		const std::vector<Segment> here = line_segments(line);
		std::size_t index = 0;
		const std::size_t base = static_cast<std::size_t>(row_prefix_[line]);
		if (row >= base)
			index = std::min(row - base, here.size() - 1);
		Row self;
		self.line = line;
		self.from = here[index].from;
		if (!want_cells_valid_)
		{
			want_cells_ = row_cells(self, byte_in_line);
			want_cells_valid_ = true;
		}
		const std::size_t rows_total = row_count();
		if (rows_total == 0)
			return;
		long target = static_cast<long>(row) + lines;
		if (target < 0)
			target = 0;
		if (static_cast<std::size_t>(target) >= rows_total)
			target = static_cast<long>(rows_total) - 1;
		const std::size_t trow = static_cast<std::size_t>(target);
		const std::size_t tline = line_of_row(trow);
		const std::vector<Segment> tsegs = line_segments(tline);
		const std::size_t tbase = static_cast<std::size_t>(row_prefix_[tline]);
		const std::size_t tindex = std::min(trow - tbase, tsegs.size() - 1);
		const Segment& seg = tsegs[tindex];
		// Клетка → байт внутри ряда.
		const std::string_view text = buf_->line_text(tline);
		const std::size_t tab = static_cast<std::size_t>(std::max(1.0F, metrics_.tab_cells));
		std::size_t cur = 0;
		std::size_t i = seg.from;
		while (i < seg.to && cur < want_cells_)
		{
			const unsigned char c = static_cast<unsigned char>(text[i]);
			const std::size_t step = next_cp(text, i) - i;
			if (c == '\t')
			{
				const std::size_t next_stop = cur + tab - (cur % tab);
				if (next_stop > want_cells_)
					break;
				cur = next_stop;
			}
			else
			{
				++cur;
			}
			i += step;
		}
		caret_ = buf_->line_start(tline) + i;
		if (!extend)
			anchor_ = caret_;
		ensure_caret_visible_impl();
		return;
	}

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

// ---- ряды переноса по словам ----

namespace {

// Сколько «клеток» в строке при данных метриках (для проверки предела).
std::size_t line_cells(std::string_view text, float tab_cells)
{
	std::size_t cells = 0;
	const std::size_t tab = static_cast<std::size_t>(std::max(1.0F, tab_cells));
	for (std::size_t i = 0; i < text.size(); ++i)
	{
		const unsigned char c = static_cast<unsigned char>(text[i]);
		if (is_cont(c))
			continue;
		if (c == '\t')
			cells += tab - (cells % tab);
		else
			++cells;
	}
	return cells;
}

} // namespace

void Editor::invalidate_rows(std::size_t from_line) const
{
	row_total_valid_ = false;
	if (from_line == 0)
	{
		row_prefix_.clear();
		row_prefix_lines_ = 0;
		return;
	}
	if (row_prefix_lines_ > from_line)
	{
		row_prefix_lines_ = from_line;
		row_prefix_.resize(from_line);
	}
}

void Editor::set_word_wrap(bool on)
{
	if (wrap_ == on)
		return;
	wrap_ = on;
	invalidate_rows(0);
	row_prefix_.clear();
	if (wrap_)
	{
		// Ширина ряда — из текущего вьюпорта (set_viewport мог быть раньше).
		wrap_cells_ = std::max<std::size_t>(
			8, static_cast<std::size_t>(std::max(0.0F, view_w_) /
										std::max(1.0F, metrics_.char_width)));
		scroll_x_ = 0.0F; // при переносе горизонтальной прокрутки нет
	}
	ensure_caret_visible_impl();
}

std::vector<Editor::Segment> Editor::line_segments(std::size_t line) const
{
	std::vector<Segment> out;
	const std::string_view text = buf_->line_text(line);
	out.push_back(Segment{ 0, text.size() });
	if (!wrap_ || text.empty())
		return out;

	const std::size_t limit = std::max<std::size_t>(8, wrap_cells_);
	// Слишком длинную строку не переносим: иначе одна строка на мегабайт
	// заставит пересчитывать сотни тысяч рядов на каждую перерисовку.
	if (line_cells(text, metrics_.tab_cells) > kWrapGuardCells)
		return out;
	// Очень большие файлы: раскладка рядов не строится (см. kWrapMaxLines).
	if (buf_->line_count() > kWrapMaxLines)
		return out;

	out.clear();
	const std::size_t tab = static_cast<std::size_t>(std::max(1.0F, metrics_.tab_cells));
	std::size_t start = 0;
	while (start < text.size())
	{
		std::size_t pos = start;
		std::size_t cells = 0;
		std::size_t fit = start;
		while (pos < text.size())
		{
			const unsigned char c = static_cast<unsigned char>(text[pos]);
			std::size_t w = 1;
			if (c == '\t')
				w = tab - (cells % tab);
			if (cells + w > limit)
				break;
			cells += w;
			pos = next_cp(text, pos);
			fit = pos;
		}
		if (pos >= text.size())
		{
			out.push_back(Segment{ start, text.size() });
			break;
		}
		// Перенос по словам: последний пробел в поместившейся части уходит
		// в конец ряда (как в редакторах: пробел не «съедает» строку).
		// Табуляция точкой переноса не считается — иначе ряд начинался бы
		// с неё, а «a\t» уезжало бы в отдельный ряд.
		std::size_t brk = std::string::npos;
		for (std::size_t k = fit; k > start;)
		{
			const std::size_t prev = prev_cp(text, k);
			if (static_cast<unsigned char>(text[prev]) == ' ')
			{
				brk = k;
				break;
			}
			k = prev;
		}
		if (brk != std::string::npos && brk > start)
		{
			out.push_back(Segment{ start, brk });
			start = brk;
			continue;
		}
		if (fit == start)
			fit = next_cp(text, start); // символ шире ряда — переносим его одного
		out.push_back(Segment{ start, fit });
		start = fit;
	}
	if (out.empty())
		out.push_back(Segment{ 0, text.size() });
	return out;
}

std::size_t Editor::rows_of_line(std::size_t line) const
{
	if (!wrap_)
		return 1;
	return line_segments(line).size();
}

void Editor::ensure_row_prefix(std::size_t line) const
{
	if (!wrap_)
		return;
	if (row_prefix_lines_ > row_prefix_.size())
	{
		row_prefix_.clear();
		row_prefix_lines_ = 0;
	}
	if (row_prefix_.empty())
	{
		row_prefix_.push_back(0);
		row_prefix_lines_ = 1;
	}
	const std::size_t lines = buf_->line_count();
	const std::size_t want = std::min(line + 1, lines);
	while (row_prefix_lines_ < want)
	{
		const std::size_t prev = row_prefix_lines_ - 1;
		const std::uint32_t next =
			row_prefix_[prev] + static_cast<std::uint32_t>(rows_of_line(prev));
		row_prefix_.push_back(next);
		++row_prefix_lines_;
		row_total_valid_ = false;
	}
}

std::size_t Editor::row_count() const
{
	if (!wrap_)
		return buf_->line_count();
	const std::size_t lines = buf_->line_count();
	if (lines == 0)
		return 0;
	ensure_row_prefix(lines - 1);
	if (!row_total_valid_)
	{
		row_total_ = row_prefix_[lines - 1] + rows_of_line(lines - 1);
		row_total_valid_ = true;
	}
	return row_total_;
}

std::size_t Editor::line_of_row(std::size_t row) const
{
	if (!wrap_)
		return std::min(row, buf_->line_count() - 1);
	const std::size_t lines = buf_->line_count();
	if (lines == 0)
		return 0;
	ensure_row_prefix(lines - 1);
	// Первая строка, начинающаяся после row, минус одна.
	std::size_t lo = 0;
	std::size_t hi = lines - 1;
	while (lo < hi)
	{
		const std::size_t mid = lo + (hi - lo + 1) / 2;
		if (static_cast<std::size_t>(row_prefix_[mid]) <= row)
			lo = mid;
		else
			hi = mid - 1;
	}
	return lo;
}

long Editor::scroll_delta_to_reveal_row(std::size_t row, bool at_bottom, int margin) const
{
	const auto first = static_cast<long long>(first_visible_row());
	const auto count = std::max<long long>(1, static_cast<long long>(visible_row_count()));
	const auto target = static_cast<long long>(row);
	const auto below = static_cast<long long>(std::max(0, margin));
	long long delta = 0;
	if (at_bottom)
	{
		// Ряд ставим так, чтобы под ним осталось `below` рядов: он оказывается
		// над панелью вопросов, а не за ней.
		delta = target + below - (first + count - 1);
	}
	else if (target < first)
		delta = target - first;
	else if (target >= first + count)
		delta = target - (first + count) + 1;
	return static_cast<long>(delta);
}

std::size_t Editor::row_of_offset(std::size_t offset) const
{
	const std::size_t line = buf_->line_of(offset);
	if (!wrap_)
		return line;
	ensure_row_prefix(line);
	const std::size_t byte_in_line = offset - buf_->line_start(line);
	const std::vector<Segment> segs = line_segments(line);
	std::size_t index = 0;
	for (std::size_t i = 0; i < segs.size(); ++i)
	{
		if (byte_in_line >= segs[i].from && byte_in_line < segs[i].to)
		{
			index = i;
			break;
		}
		index = i; // курсор в конце строки — последний ряд
	}
	return static_cast<std::size_t>(row_prefix_[line]) + index;
}

std::size_t Editor::row_cells(const Row& row, std::size_t byte_in_line) const
{
	const std::string_view text = buf_->line_text(row.line);
	const std::size_t from = std::min(row.from, text.size());
	const std::size_t to = std::min(std::max(byte_in_line, from), text.size());
	const std::size_t tab = static_cast<std::size_t>(std::max(1.0F, metrics_.tab_cells));
	std::size_t cells = 0;
	std::size_t i = from;
	while (i < to)
	{
		const unsigned char c = static_cast<unsigned char>(text[i]);
		if (c == '\t')
			cells += tab - (cells % tab);
		else
			++cells;
		i = next_cp(text, i);
	}
	return cells;
}

float Editor::row_width(const Row& row, std::size_t byte_in_line) const
{
	return static_cast<float>(row_cells(row, byte_in_line)) * std::max(1.0F, metrics_.char_width);
}

std::size_t Editor::first_visible_row() const
{
	const float lh = std::max(1.0F, metrics_.line_height);
	const std::size_t rows_total = wrap_ ? row_count() : buf_->line_count();
	if (rows_total == 0)
		return 0;
	const std::size_t row = static_cast<std::size_t>(std::max(0.0F, scroll_y_) / lh);
	return std::min(row, rows_total - 1);
}

std::size_t Editor::visible_row_count() const
{
	const float lh = std::max(1.0F, metrics_.line_height);
	const std::size_t rows = static_cast<std::size_t>(std::ceil(view_h_ / lh)) + 1;
	const std::size_t total = wrap_ ? row_count() : buf_->line_count();
	const std::size_t first = first_visible_row();
	return total > first ? std::min(rows, total - first) : 1;
}

std::vector<Editor::Row> Editor::visible_rows() const
{
	std::vector<Row> out;
	const std::size_t lines = buf_->line_count();
	if (lines == 0)
		return out;
	const float lh = std::max(1.0F, metrics_.line_height);
	const std::size_t rows_total = row_count();
	const std::size_t first_row =
		std::min(static_cast<std::size_t>(std::max(0.0F, scroll_y_) / lh),
				 rows_total > 0 ? rows_total - 1 : 0);
	std::size_t row = first_row;
	const std::size_t rows_to_cover = static_cast<std::size_t>(std::ceil(view_h_ / lh)) + 2;
	while (row < rows_total && row < first_row + rows_to_cover)
	{
		const std::size_t line = line_of_row(row);
		const std::vector<Segment> segs = line_segments(line);
		// Без переноса ряд совпадает со строкой, и таблицы row_prefix_ нет
		// (она строится только в режиме переноса) — берём номер строки.
		const std::size_t base =
			wrap_ ? static_cast<std::size_t>(row_prefix_[line]) : line;
		for (std::size_t i = row - base; i < segs.size() && row < rows_total; ++i, ++row)
		{
			Row r;
			r.line = line;
			r.from = segs[i].from;
			r.to = segs[i].to;
			r.top = static_cast<float>(row) * lh;
			r.first_of_line = i == 0;
			r.last_of_line = i + 1 >= segs.size();
			out.push_back(r);
		}
		if (segs.empty())
			++row;
	}
	return out;
}

void Editor::after_edit(std::size_t edited)
{
	anchor_ = caret_;
	if (wrap_)
		invalidate_rows(buf_->line_of(edited));
	want_cells_valid_ = false;
	// Кэш ширины — максимум по строкам, и после правки он может только
	// уменьшиться: удалили длинную строку, короткую вставили вместо неё.
	// Раньше кэш спускался только в set_metrics, и после удаления самой
	// длинной строки горизонтальная полоса прокрутки висели в окне
	// без текста. Пересчитываем при первом обращении — это проход по
	// первому kWidthScanLines строкам, а не по всему документу.
	width_dirty_ = true;
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
	if (!wrap_)
		return prefix_width(line, caret_ - buf_->line_start(line));
	const std::size_t byte_in_line = caret_ - buf_->line_start(line);
	const std::vector<Segment> segs = line_segments(line);
	const std::size_t index = [&]
	{
		for (std::size_t i = 0; i < segs.size(); ++i)
		{
			if (byte_in_line >= segs[i].from && byte_in_line < segs[i].to)
				return i;
		}
		return segs.size() - 1;
	}();
	Row row;
	row.line = line;
	row.from = segs[index].from;
	row.to = segs[index].to;
	return row_width(row, byte_in_line);
}

float Editor::caret_y() const
{
	if (!wrap_)
		return line_top(buf_->line_of(caret_));
	return static_cast<float>(row_of_offset(caret_)) * std::max(1.0F, metrics_.line_height);
}

float Editor::line_top(std::size_t line) const
{
	if (!wrap_)
		return static_cast<float>(line) * metrics_.line_height;
	ensure_row_prefix(line);
	const float row = line < row_prefix_.size() ? static_cast<float>(row_prefix_[line])
												: static_cast<float>(line);
	return row * ::std::max(1.0F, metrics_.line_height);
}

std::size_t Editor::offset_at_client(float screen_x, float screen_y, float padding_x) const
{
	return offset_at(screen_x - padding_x + scroll_x_, screen_y + scroll_y_);
}

std::size_t Editor::offset_at(float x, float y) const
{
	if (buf_->line_count() == 0)
		return 0;
	if (wrap_)
	{
		const float lh = std::max(1.0F, metrics_.line_height);
		const std::size_t rows_total = row_count();
		if (rows_total == 0)
			return 0;
		long row = static_cast<long>(y / lh);
		if (row < 0)
			row = 0;
		if (static_cast<std::size_t>(row) >= rows_total)
			row = static_cast<long>(rows_total) - 1;
		const std::size_t line = line_of_row(static_cast<std::size_t>(row));
		const std::vector<Segment> segs = line_segments(line);
		const std::size_t base = static_cast<std::size_t>(row_prefix_[line]);
		const std::size_t index =
			std::min(static_cast<std::size_t>(row) - base, segs.size() - 1);
		const Segment& seg = segs[index];
		// Клетки считаем внутри ряда: табуляции отсчитываются от его начала.
		const float cw = std::max(1.0F, metrics_.char_width);
		const long cells = static_cast<long>(x / cw + 0.5F);
		const std::size_t want = cells > 0 ? static_cast<std::size_t>(cells) : 0;
		const std::string_view text = buf_->line_text(line);
		const std::size_t tab = static_cast<std::size_t>(std::max(1.0F, metrics_.tab_cells));
		std::size_t cur = 0;
		std::size_t i = seg.from;
		while (i < seg.to)
		{
			if (cur >= want)
				break;
			const unsigned char c = static_cast<unsigned char>(text[i]);
			const std::size_t step = next_cp(text, i) - i;
			if (c == '\t')
			{
				const std::size_t next_stop = cur + tab - (cur % tab);
				if (next_stop > want)
					break;
				cur = next_stop;
			}
			else
			{
				++cur;
			}
			i += step;
		}
		return buf_->line_start(line) + i;
	}
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
	const float old_cells = static_cast<float>(wrap_cells_);
	view_w_ = std::max(0.0F, width);
	view_h_ = std::max(0.0F, height);
	if (wrap_)
	{
		// Ширина окна задаёт, сколько «клеток» влезает в ряд.
		wrap_cells_ = std::max<std::size_t>(
			8, static_cast<std::size_t>(std::max(0.0F, view_w_) /
										std::max(1.0F, metrics_.char_width)));
		if (static_cast<float>(wrap_cells_) != old_cells)
		{
			invalidate_rows(0);
			row_prefix_.clear();
		}
		scroll_x_ = 0.0F;
	}
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
	if (wrap_)
	{
		scroll_x_ = 0.0F;
		const float max_y = std::max(0.0F, content_height() - view_h_);
		scroll_y_ = std::max(0.0F, std::min(scroll_y_ + dy, max_y));
		return;
	}
	const float max_x = std::max(0.0F, text_width() - view_w_ + metrics_.char_width);
	scroll_x_ = std::max(0.0F, std::min(scroll_x_ + dx, max_x));
	const float max_y = std::max(0.0F, content_height() - view_h_);
	scroll_y_ = std::max(0.0F, std::min(scroll_y_ + dy, max_y));
}

void Editor::scroll_lines(long lines)
{
	scroll_by(0.0F, static_cast<float>(lines) * metrics_.line_height);
}

void Editor::set_scroll_y(float y)
{
	scroll_y_ = std::max(0.0F, std::min(y, std::max(0.0F, content_height() - view_h_)));
}

void Editor::set_scroll_x(float x)
{
	if (wrap_)
	{
		scroll_x_ = 0.0F;
		return;
	}
	scroll_x_ = std::max(0.0F, std::min(x, std::max(0.0F, text_width() - view_w_ + metrics_.char_width)));
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
	if (!wrap_)
		return std::min(line, buf_->line_count() - 1);
	const std::size_t rows_total = row_count();
	if (rows_total == 0)
		return 0;
	return line_of_row(std::min(line, rows_total - 1));
}

std::size_t Editor::visible_line_count() const
{
	const float lh = std::max(1.0F, metrics_.line_height);
	const std::size_t lines = static_cast<std::size_t>(std::ceil(view_h_ / lh)) + 1;
	const std::size_t first = first_visible_line();
	if (!wrap_)
		return std::min(lines, buf_->line_count() - first);
	// Сколько логических строк покрывают видимые ряды.
	const std::size_t rows_total = row_count();
	const std::size_t first_row =
		std::min(static_cast<std::size_t>(std::max(0.0F, scroll_y_) / lh),
				 rows_total > 0 ? rows_total - 1 : 0);
	const std::size_t last_row = std::min(first_row + lines, rows_total > 0 ? rows_total - 1 : 0);
	const std::size_t last_line = line_of_row(last_row);
	return last_line + 1 > first ? last_line + 1 - first : 1;
}

float Editor::content_height() const
{
	const std::size_t rows = wrap_ ? row_count() : buf_->line_count();
	return static_cast<float>(rows) * std::max(1.0F, metrics_.line_height);
}

namespace {
// Сколько строк просматривать при первом вычислении ширины текста.
constexpr std::size_t kWidthScanLines = 5000;
} // namespace

float Editor::text_width() const
{
	if (wrap_)
		return view_w_; // при переносе горизонтальной прокрутки нет
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
