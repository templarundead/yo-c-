// Редактор поверх TextBuffer: курсор, выделение, прокрутка, геометрия строк.
//
// Модуль намеренно не знает о Win32: вся арифметика (позиция курсора,
// попадание мышью в байтовое смещение, прокрутка, ленивая выдача видимых
// строк) живёт здесь и покрыта обычными тестами. Win32-слой только рисует
// и переводит сообщения в вызовы этого класса.
//
// Шрифт в MVP моноширинный: строка занимает line_height пикселей, символ —
// char_width пикселей (табуляция расширяется до следующей границы tab_cells).
// Благодаря этому раскладка детерминирована и не требует DirectWrite для
// измерений; пропорциональный шрифт — отдельный этап (см. ARCHITECTURE.md §3).
#pragma once

#include "yo/textbuf.hpp"

#include <cstddef>
#include <memory>
#include <string>
#include <string_view>

namespace yo {

struct Metrics
{
	float line_height = 17.0F; // высота строки, px
	float char_width = 8.0F;   // ширина моноширинного символа, px
	float tab_cells = 4.0F;    // табуляция: до кратной границы в «клетках»
	float padding_x = 8.0F;    // отступ текста от края окна, px
};

// Позиция курсора для статусбара: строка с 1, столбец в символах с 1.
struct CaretPos
{
	std::size_t line = 1;
	std::size_t column = 1;
};

class Editor
{
public:
	explicit Editor(std::shared_ptr<TextBuffer> buffer);

	[[nodiscard]] TextBuffer& buffer() noexcept { return *buf_; }
	[[nodiscard]] const TextBuffer& buffer() const noexcept { return *buf_; }
	[[nodiscard]] const Metrics& metrics() const noexcept { return metrics_; }
	void set_metrics(const Metrics& m) noexcept;

	// --- курсор и выделение ---
	[[nodiscard]] std::size_t caret() const noexcept { return caret_; }
	[[nodiscard]] std::size_t anchor() const noexcept { return anchor_; }
	[[nodiscard]] bool has_selection() const noexcept { return anchor_ != caret_; }
	[[nodiscard]] std::size_t sel_begin() const noexcept;
	[[nodiscard]] std::size_t sel_end() const noexcept;
	[[nodiscard]] std::string selected_text() const;
	[[nodiscard]] CaretPos caret_pos() const;

	void set_caret(std::size_t offset, bool extend = false);
	void select_all();
	void clear_selection() { anchor_ = caret_; }

	void move_left(bool extend = false, bool by_word = false);
	void move_right(bool extend = false, bool by_word = false);
	void move_up(bool extend = false);
	void move_down(bool extend = false);
	void move_line_start(bool extend = false);
	void move_line_end(bool extend = false);
	void move_doc_start(bool extend = false);
	void move_doc_end(bool extend = false);
	void move_page(long pages, bool extend = false);
	void click_at(float x, float y, bool extend = false);

	// --- правки (заменяют выделение) ---
	bool insert_text(std::string_view utf8);
	bool insert_char(char32_t cp); // кодирует в UTF-8 и вставляет
	bool backspace();				// удаляет выделение или символ слева
	bool delete_forward();			// удаляет выделение или символ справа
	bool delete_selection();
	bool cut_selection(std::string* out);
	bool copy_selection(std::string* out) const;
	bool paste(std::string_view utf8);

	void begin_group(std::string_view name) { buf_->begin_group(name); }
	void end_group() { buf_->end_group(); }

	// --- вид и прокрутка ---
	void set_viewport(float width, float height);
	[[nodiscard]] float viewport_width() const noexcept { return view_w_; }
	[[nodiscard]] float viewport_height() const noexcept { return view_h_; }

	[[nodiscard]] float scroll_x() const noexcept { return scroll_x_; }
	[[nodiscard]] float scroll_y() const noexcept { return scroll_y_; }
	void scroll_by(float dx, float dy);
	void scroll_lines(long lines);
	void ensure_caret_visible();

	[[nodiscard]] std::size_t first_visible_line() const;
	[[nodiscard]] std::size_t visible_line_count() const;
	[[nodiscard]] float content_height() const;
	// Ширина самой длинной известной строки, px. Полный проход по документу
	// не делается: при первом обращении просматриваются первые
	// kWidthScanLines строк, дальше максимум уточняется по мере отрисовки
	// (note_line_cells). Для горизонтальной полосы прокрутки этого достаточно.
	[[nodiscard]] float text_width() const;
	void note_line_cells(std::size_t cells) const;

	// --- геометрия ---
	// Координаты в системе документа (без отступа окна и без прокрутки):
	// рендерер сам прибавляет padding и вычитает scroll.
	[[nodiscard]] float caret_x() const;
	[[nodiscard]] float caret_y() const;
	[[nodiscard]] float line_top(std::size_t line) const;
	[[nodiscard]] std::size_t offset_at(float x, float y) const;
	// Смещение под точкой клиента: экранные координаты → документ.
	// Рендерер рисует строку i в line_top(i) - scroll_y, поэтому обратное
	// преобразование прибавляет прокрутку (без этого курсор вставал выше
	// клика на 2*scroll_y/lh строк — «выделение съезжало»).
	[[nodiscard]] std::size_t offset_at_client(float screen_x, float screen_y,
											  float padding_x = 0.0F) const;
	// Ширина префикса строки до байта `byte_in_line` в пикселях.
	[[nodiscard]] float prefix_width(std::size_t line, std::size_t byte_in_line) const;
	// «Клетки» (символьные колонки) строки до байта byte_in_line.
	[[nodiscard]] std::size_t prefix_cells(std::size_t line, std::size_t byte_in_line) const;

private:
	void move_vertically(long lines, bool extend);
	void ensure_caret_visible_impl();
	[[nodiscard]] std::size_t line_of_offset(std::size_t offset) const;
	[[nodiscard]] std::size_t clamp_offset(std::size_t offset) const;
	[[nodiscard]] std::size_t clamp_scroll_y(float y) const;
	[[nodiscard]] std::size_t cells_to_byte(std::size_t line, std::size_t cells) const;
	void after_edit(std::size_t caret);
	void invalidate_width();

	std::shared_ptr<TextBuffer> buf_;
	Metrics metrics_;
	std::size_t caret_ = 0;
	std::size_t anchor_ = 0;
	// Желаемая «клетка» при вертикальном движении (как в редакторах).
	std::size_t want_cells_ = 0;
	bool want_cells_valid_ = false;

	float view_w_ = 0.0F;
	float view_h_ = 0.0F;
	float scroll_x_ = 0.0F;
	float scroll_y_ = 0.0F;

	mutable float cached_text_width_ = 0.0F;
	mutable bool width_dirty_ = true;
};

} // namespace yo
