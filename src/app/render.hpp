// Рендеринг редактора: Direct2D + DirectWrite.
//
// Слой тонкий: раскладка (что видно, где курсор, где выделение) целиком
// в yo::Editor, здесь — только рисование видимых строк и полос статуса.
// Шрифт моноширинный, поэтому ширина символа измеряется один раз при
// смене шрифта и используется и рендерером, и редактором (Metrics).
#pragma once

#include "yo/editor.hpp"

#include <windows.h>

#include <d2d1.h>
#include <dwrite.h>

#include <cstddef>
#include <string>
#include <vector>

namespace yo::gui {

// Панель вопросов ёфикации: строки текста и кнопки в клиентских пикселях.
// Геометрию считает App (ей же нужны прямоугольники для попадания мышью),
// Renderer только рисует — так раскладка кнопок живёт в одном месте.
enum class AskKind
{
	normal, // основной текст (варианты написания)
	accent, // причина вопроса
	dim,	// строка контекста из документа
};

struct AskRow
{
	D2D1_RECT_F rect{};
	std::wstring text;
	AskKind kind = AskKind::normal;
};

struct AskButtonView
{
	D2D1_RECT_F rect{};
	std::wstring label;
	bool focus = false;
};

struct AskView
{
	D2D1_RECT_F panel{};
	std::vector<AskRow> rows;
	std::vector<AskButtonView> buttons;
};

struct Theme
{
	D2D1_COLOR_F bg = D2D1::ColorF(0.086F, 0.090F, 0.102F);
	D2D1_COLOR_F text = D2D1::ColorF(0.83F, 0.85F, 0.88F);
	D2D1_COLOR_F gutter = D2D1::ColorF(0.13F, 0.14F, 0.16F);
	D2D1_COLOR_F selection = D2D1::ColorF(0.17F, 0.31F, 0.50F);
	D2D1_COLOR_F caret = D2D1::ColorF(0.96F, 0.96F, 0.96F);
	D2D1_COLOR_F status_bg = D2D1::ColorF(0.14F, 0.15F, 0.17F);
	D2D1_COLOR_F status_fg = D2D1::ColorF(0.66F, 0.69F, 0.72F);
	D2D1_COLOR_F accent = D2D1::ColorF(0.36F, 0.62F, 0.98F);
};

class Renderer
{
public:
	Renderer() = default;
	~Renderer();
	Renderer(const Renderer&) = delete;
	Renderer& operator=(const Renderer&) = delete;

	// Создаёт фабрики и ресурсы под окно. false — Direct2D не поднялся.
	bool init(HWND hwnd, float font_size);
	void shutdown();

	// Размер клиентской области в физических пикселях и масштаб DPI.
	void resize(UINT width_px, UINT height_px);
	void set_dpi(float scale);

	bool set_font_size(float size_dip, HWND hwnd);
	[[nodiscard]] float font_size() const noexcept { return font_size_; }
	[[nodiscard]] float char_width() const noexcept { return char_w_; }
	[[nodiscard]] float padding() const noexcept { return padding_; }
	[[nodiscard]] float line_height() const noexcept { return line_h_; }

	// Высота нижней полосы (статус + строка поиска) в пикселях.
	[[nodiscard]] float status_height() const noexcept;
	[[nodiscard]] float find_height() const noexcept;

	// Ширина строки шрифтом интерфейса (Segoe UI) в пикселях — для кнопок.
	[[nodiscard]] float measure_ui(const std::wstring& text);

	// Полная перерисовка. `caret` — рисовать ли курсор (мигание),
	// `ask` — панель вопросов ёфикации поверх текста.
	void paint(Editor& ed, const std::wstring& status, bool show_find_bar,
			   const std::wstring& find_text, bool find_focus, bool caret,
			   const AskView* ask = nullptr);

	[[nodiscard]] float text_area_width() const noexcept;
	[[nodiscard]] float text_area_height() const noexcept;

private:
	bool ensure_resources(HWND hwnd);
	bool measure_font(HWND hwnd);
	bool create_render_target(HWND hwnd);
	void draw_status(const std::wstring& status);
	void draw_find_bar(const std::wstring& text, bool focus);
	void draw_ask(const AskView& view);

	HWND hwnd_ = nullptr;
	ID2D1Factory* factory_ = nullptr;
	ID2D1HwndRenderTarget* rt_ = nullptr;
	IDWriteFactory* dwrite_ = nullptr;
	IDWriteTextFormat* fmt_ = nullptr;		// основной текст
	IDWriteTextFormat* fmt_ui_ = nullptr;	// статус/поиск
	ID2D1SolidColorBrush* brush_ = nullptr;
	ID2D1SolidColorBrush* accent_brush_ = nullptr; // подсветка ё
	ID2D1SolidColorBrush* dim_brush_ = nullptr;	   // затемнение под панелью вопросов

	float dpi_scale_ = 1.0F;
	float font_size_ = 15.0F;
	float char_w_ = 8.0F;
	float line_h_ = 18.0F;
	float status_h_ = 22.0F;
	float find_h_ = 26.0F;
	float padding_ = 6.0F;
	UINT width_px_ = 0;
	UINT height_px_ = 0;

	struct CachedLine
	{
		std::size_t line = 0;
		std::size_t from = 0; // смещение среза в байтах (длинные строки)
		std::wstring text;
		IDWriteTextLayout* layout = nullptr;
		bool used = false;
	};
	std::vector<CachedLine> cache_;

	Theme theme_;
};

} // namespace yo::gui
