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
	[[nodiscard]] float line_height() const noexcept { return line_h_; }

	// Высота нижней полосы (статус + строка поиска) в пикселях.
	[[nodiscard]] float status_height() const noexcept;
	[[nodiscard]] float find_height() const noexcept;

	// Полная перерисовка. `caret` — рисовать ли курсор (мигание).
	void paint(Editor& ed, const std::wstring& status, bool show_find_bar,
			   const std::wstring& find_text, bool find_focus, bool caret);

	[[nodiscard]] float text_area_width() const noexcept;
	[[nodiscard]] float text_area_height() const noexcept;

private:
	bool ensure_resources(HWND hwnd);
	bool measure_font(HWND hwnd);
	bool create_render_target(HWND hwnd);
	void draw_status(const std::wstring& status);
	void draw_find_bar(const std::wstring& text, bool focus);

	HWND hwnd_ = nullptr;
	ID2D1Factory* factory_ = nullptr;
	ID2D1HwndRenderTarget* rt_ = nullptr;
	IDWriteFactory* dwrite_ = nullptr;
	IDWriteTextFormat* fmt_ = nullptr;		// основной текст
	IDWriteTextFormat* fmt_ui_ = nullptr;	// статус/поиск
	ID2D1SolidColorBrush* brush_ = nullptr;

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
		std::wstring text;
		IDWriteTextLayout* layout = nullptr;
		bool used = false;
	};
	std::vector<CachedLine> cache_;

	Theme theme_;
};

} // namespace yo::gui
