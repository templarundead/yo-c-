#include "render.hpp"

#include "yo/utf.hpp"

#include <algorithm>
#include <cmath>

namespace yo::gui {

namespace {

constexpr UINT kDpiDefault = 96;

// Строки длиннее этого раскладываются только видимым срезом (см. paint).
constexpr std::size_t kLineSliceBytes = 4096;
// Запас символов слева и справа от видимой области среза.
constexpr float kSliceMarginCells = 64.0F;

} // namespace

Renderer::~Renderer() { shutdown(); }

void Renderer::shutdown()
{
	for (CachedLine& c : cache_)
	{
		if (c.layout != nullptr)
			c.layout->Release();
	}
	cache_.clear();
	if (brush_ != nullptr)
	{
		brush_->Release();
		brush_ = nullptr;
	}
	if (accent_brush_ != nullptr)
	{
		accent_brush_->Release();
		accent_brush_ = nullptr;
	}
	if (dim_brush_ != nullptr)
	{
		dim_brush_->Release();
		dim_brush_ = nullptr;
	}
	if (fmt_ != nullptr)
	{
		fmt_->Release();
		fmt_ = nullptr;
	}
	if (fmt_ui_ != nullptr)
	{
		fmt_ui_->Release();
		fmt_ui_ = nullptr;
	}
	if (rt_ != nullptr)
	{
		rt_->Release();
		rt_ = nullptr;
	}
	if (dwrite_ != nullptr)
	{
		dwrite_->Release();
		dwrite_ = nullptr;
	}
	if (factory_ != nullptr)
	{
		factory_->Release();
		factory_ = nullptr;
	}
	hwnd_ = nullptr;
}

bool Renderer::init(HWND hwnd, float font_size)
{
	hwnd_ = hwnd;
	font_size_ = font_size;
	if (FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, __uuidof(ID2D1Factory), nullptr,
								 reinterpret_cast<void**>(&factory_))))
		return false;
	if (FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
								   reinterpret_cast<IUnknown**>(&dwrite_))))
		return false;
	if (!measure_font(hwnd))
		return false;
	rt_ = nullptr;
	return true;
}

void Renderer::set_dpi(float scale)
{
	if (scale <= 0.0F)
		return;
	dpi_scale_ = scale;
	width_dip_ = static_cast<float>(width_px_) / scale;
	height_dip_ = static_cast<float>(height_px_) / scale;
	// Шрифт задаётся в DIP, поэтому размеры пересчитываются.
	if (hwnd_ != nullptr)
	{
		if (!measure_font(hwnd_))
			return;
		if (rt_ != nullptr)
			rt_->SetDpi(static_cast<float>(kDpiDefault) * dpi_scale_, static_cast<float>(kDpiDefault) * dpi_scale_);
	}
}

bool Renderer::measure_font(HWND hwnd)
{
	if (fmt_ != nullptr)
	{
		fmt_->Release();
		fmt_ = nullptr;
	}
	if (fmt_ui_ != nullptr)
	{
		fmt_ui_->Release();
		fmt_ui_ = nullptr;
	}
	// Моноширинный шрифт: Cascadia Mono есть в Windows 11/10 (Terminal),
	// Consolas — надёжный запасной вариант, дальше DirectWrite сам подберёт.
	const wchar_t* families[] = { L"Cascadia Mono", L"Consolas", L"Courier New" };
	const float size = font_size_;
	for (const wchar_t* family : families)
	{
		if (SUCCEEDED(dwrite_->CreateTextFormat(family, nullptr, DWRITE_FONT_WEIGHT_NORMAL,
												DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, size,
												L"ru-ru", &fmt_)))
			break;
	}
	if (fmt_ == nullptr)
		return false;
	fmt_->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
	fmt_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
	fmt_->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);

	if (FAILED(dwrite_->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_NORMAL,
										 DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 12.0F, L"ru-ru",
										 &fmt_ui_)))
		return false;
	fmt_ui_->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
	fmt_ui_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
	fmt_ui_->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

	// Ширина символа: 64 одинаковых глифа делим на 64.
	const std::wstring probe(64, L'0');
	IDWriteTextLayout* layout = nullptr;
	if (FAILED(dwrite_->CreateTextLayout(probe.c_str(), static_cast<UINT32>(probe.size()), fmt_, 4096.0F,
										 4096.0F, &layout)))
		return false;
	DWRITE_TEXT_METRICS tm{};
	const HRESULT hr = layout->GetMetrics(&tm);
	layout->Release();
	if (FAILED(hr))
		return false;

	char_w_ = std::max(1.0F, tm.widthIncludingTrailingWhitespace / 64.0F);
	line_h_ = std::max(1.0F, std::ceil(tm.height));

	// Всё рисуется в DIP, а целевой буфер сам масштабирует DIP → пиксели
	// (SetDpi). Умножать размеры на dpi_scale_ нельзя: полоса статуса уезжала
	// за нижний край, а метрики строк не совпадали с координатами мыши.
	padding_ = 6.0F;
	status_h_ = 20.0F;
	find_h_ = 26.0F;
	bar_h_ = 26.0F;

	if (hwnd_ == nullptr)
		hwnd_ = hwnd;
	if (rt_ != nullptr)
		rt_->SetDpi(static_cast<float>(kDpiDefault) * dpi_scale_, static_cast<float>(kDpiDefault) * dpi_scale_);
	return true;
}

bool Renderer::set_font_size(float size_dip, HWND hwnd)
{
	font_size_ = std::clamp(size_dip, 8.0F, 48.0F);
	if (dwrite_ == nullptr)
		return false;
	if (hwnd != nullptr)
		hwnd_ = hwnd;
	if (!measure_font(hwnd_))
		return false;
	// Кэш раскладок устарел: геометрия шрифта изменилась.
	for (CachedLine& c : cache_)
	{
		if (c.layout != nullptr)
			c.layout->Release();
	}
	cache_.clear();
	return true;
}

bool Renderer::create_render_target(HWND hwnd)
{
	if (rt_ != nullptr)
	{
		rt_->Release();
		rt_ = nullptr;
	}
	if (brush_ != nullptr)
	{
		brush_->Release();
		brush_ = nullptr;
	}
	if (accent_brush_ != nullptr)
	{
		accent_brush_->Release();
		accent_brush_ = nullptr;
	}
	if (dim_brush_ != nullptr)
	{
		dim_brush_->Release();
		dim_brush_ = nullptr;
	}
	// Кэш раскладок ссылается на кисти подсветки — сбрасываем его вместе
	// с целевым буфером.
	for (CachedLine& c : cache_)
	{
		if (c.layout != nullptr)
			c.layout->Release();
	}
	cache_.clear();
	if (factory_ == nullptr || hwnd == nullptr)
		return false;
	RECT rc{};
	GetClientRect(hwnd, &rc);
	const D2D1_SIZE_U size = D2D1::SizeU(static_cast<UINT32>(std::max<LONG>(1, rc.right - rc.left)),
										 static_cast<UINT32>(std::max<LONG>(1, rc.bottom - rc.top)));
	const auto rt_props = D2D1::RenderTargetProperties(
		D2D1_RENDER_TARGET_TYPE_DEFAULT,
		D2D1::PixelFormat(DXGI_FORMAT_UNKNOWN, D2D1_ALPHA_MODE_UNKNOWN),
		static_cast<float>(kDpiDefault) * dpi_scale_, static_cast<float>(kDpiDefault) * dpi_scale_);
	const auto hwnd_props = D2D1::HwndRenderTargetProperties(hwnd, size, D2D1_PRESENT_OPTIONS_NONE);
	if (FAILED(factory_->CreateHwndRenderTarget(rt_props, hwnd_props, &rt_)))
		return false;
	if (FAILED(rt_->CreateSolidColorBrush(theme_.text, &brush_)))
		return false;
	if (FAILED(rt_->CreateSolidColorBrush(D2D1::ColorF(0.0F, 0.0F, 0.0F, 0.45F), &dim_brush_)))
		dim_brush_ = nullptr;
	return SUCCEEDED(rt_->CreateSolidColorBrush(theme_.accent, &accent_brush_));
}

void Renderer::resize(UINT width_px, UINT height_px)
{
	const bool same = rt_ != nullptr && width_px == width_px_ && height_px == height_px_;
	width_px_ = width_px;
	height_px_ = height_px;
	const float scale = dpi_scale_ > 0.0F ? dpi_scale_ : 1.0F;
	width_dip_ = static_cast<float>(width_px) / scale;
	height_dip_ = static_cast<float>(height_px) / scale;
	if (same)
		return; // размер не менялся: Resize не нужен
	if (rt_ == nullptr && !create_render_target(hwnd_))
		return;
	rt_->Resize(D2D1::SizeU(std::max<UINT32>(1, width_px), std::max<UINT32>(1, height_px)));
}

float Renderer::status_height() const noexcept { return status_h_; }
float Renderer::find_height() const noexcept { return find_h_; }

float Renderer::text_area_width() const noexcept
{
	return std::max(0.0F, width_dip_ - 2.0F * padding_);
}

float Renderer::text_area_height() const noexcept
{
	return std::max(0.0F, height_dip_ - bar_h_ - status_h_);
}

void Renderer::draw_status(const std::wstring& status)
{
	if (rt_ == nullptr || fmt_ui_ == nullptr || brush_ == nullptr)
		return;
	const float bar_top = height_dip_ - status_h_;
	// Статус — своя полоса с фоном и линией-разделителем. Без этого низ окна
	// сливался с текстом, а на светлой теме выглядел «белым» (жалоба на бар).
	brush_->SetColor(theme_.status_bg);
	rt_->FillRectangle(D2D1::RectF(0.0F, bar_top, width_dip_, height_dip_),
					   brush_);
	brush_->SetColor(theme_.gutter);
	rt_->FillRectangle(D2D1::RectF(0.0F, bar_top, width_dip_, bar_top + 1.0F), brush_);
	if (status.empty())
		return;
	brush_->SetColor(theme_.status_fg);
	const float top = height_dip_ - status_h_;
	const D2D1_RECT_F rect = D2D1::RectF(padding_, top, width_dip_ - padding_,
										 height_dip_);
	rt_->DrawTextW(status.c_str(), static_cast<UINT32>(status.size()), fmt_ui_, rect, brush_,
				   D2D1_DRAW_TEXT_OPTIONS_CLIP);
}

void Renderer::draw_find_bar(const std::wstring& text, bool focus)
{
	if (rt_ == nullptr || fmt_ui_ == nullptr || brush_ == nullptr)
		return;
	const float top = height_dip_ - status_h_ - find_h_;
	brush_->SetColor(theme_.gutter);
	rt_->FillRectangle(D2D1::RectF(0.0F, top, width_dip_, top + find_h_), brush_);
	brush_->SetColor(focus ? theme_.accent : theme_.status_fg);
	rt_->DrawRectangle(
		D2D1::RectF(padding_ - 2.0F, top + 2.0F, width_dip_ - padding_ + 2.0F,
					top + find_h_ - 2.0F),
		brush_, 1.0F);
	brush_->SetColor(theme_.text);
	const std::wstring line = L"Найти: " + text;
	rt_->DrawTextW(line.c_str(), static_cast<UINT32>(line.size()), fmt_ui_,
				   D2D1::RectF(padding_ + 2.0F, top + 2.0F, width_dip_ - padding_,
							   top + find_h_ - 2.0F),
				   brush_, D2D1_DRAW_TEXT_OPTIONS_CLIP);
}

float Renderer::measure_ui(const std::wstring& text)
{
	if (dwrite_ == nullptr || fmt_ui_ == nullptr || text.empty())
		return 0.0F;
	IDWriteTextLayout* layout = nullptr;
	if (FAILED(dwrite_->CreateTextLayout(text.c_str(), static_cast<UINT32>(text.size()), fmt_ui_,
										 4096.0F, line_h_, &layout)) ||
		layout == nullptr)
		return 0.0F;
	DWRITE_TEXT_METRICS m{};
	const float width = SUCCEEDED(layout->GetMetrics(&m)) ? m.widthIncludingTrailingWhitespace : 0.0F;
	layout->Release();
	return width;
}

void Renderer::draw_word_highlight(Editor& ed, float scroll_x, float scroll_y, float text_h,
								   std::size_t from, std::size_t to)
{
	if (rt_ == nullptr || brush_ == nullptr || from >= to)
		return;
	const float lh = std::max(1.0F, ed.metrics().line_height);
	const std::size_t first = ed.first_visible_line();
	const std::size_t count = ed.visible_line_count();
	brush_->SetColor(D2D1::ColorF(0.72F, 0.16F, 0.16F, 0.85F)); // как «красное» слово оригинала
	for (std::size_t i = first; i < first + count && i < ed.buffer().line_count(); ++i)
	{
		const std::size_t line_start = ed.buffer().line_start(i);
		const std::size_t line_len = ed.buffer().line_text(i).size();
		const std::size_t line_end = line_start + line_len;
		if (line_end < from || line_start > to)
			continue;
		const std::size_t a = from > line_start ? from - line_start : 0;
		const std::size_t b = std::min(to - line_start, line_len);
		if (b <= a)
			continue;
		const float x1 = padding_ + ed.prefix_width(i, a) - scroll_x;
		const float x2 = padding_ + ed.prefix_width(i, b) - scroll_x;
		const float y = bar_h_ + ed.line_top(i) - scroll_y;
		if (y + lh < bar_h_ || y > bar_h_ + text_h)
			continue;
		rt_->FillRectangle(D2D1::RectF(x1, y + 1.0F, std::max(x2, x1 + 1.0F), y + lh - 1.0F), brush_);
	}
}

void Renderer::draw_menu_bar(const MenuBarView& bar)
{
	if (rt_ == nullptr || fmt_ui_ == nullptr || brush_ == nullptr)
		return;

	// Фон строки меню и линия-разделитель: системное меню на тёмной теме
	// остаётся белым, поэтому рисуем строку сами.
	brush_->SetColor(theme_.gutter);
	rt_->FillRectangle(D2D1::RectF(0.0F, 0.0F, width_dip_, bar_h_), brush_);
	brush_->SetColor(D2D1::ColorF(0.05F, 0.05F, 0.06F));
	rt_->FillRectangle(D2D1::RectF(0.0F, bar_h_ - 1.0F, width_dip_, bar_h_), brush_);

	for (std::size_t i = 0; i < bar.titles.size() && i < bar.title_rects.size(); ++i)
	{
		const D2D1_RECT_F& r = bar.title_rects[i];
		const bool lit = static_cast<int>(i) == bar.active || static_cast<int>(i) == bar.hot_title;
		if (lit)
		{
			brush_->SetColor(bar.active == static_cast<int>(i) ? theme_.selection : theme_.status_bg);
			rt_->FillRectangle(r, brush_);
		}
		brush_->SetColor(theme_.text);
		rt_->DrawTextW(bar.titles[i].c_str(), static_cast<UINT32>(bar.titles[i].size()), fmt_ui_, r, brush_,
					   D2D1_DRAW_TEXT_OPTIONS_CLIP);
	}

	if (!bar.dropdown)
		return;

	// Выпадающий список: фон, рамка, пункты, галочки и разделители.
	brush_->SetColor(theme_.status_bg);
	rt_->FillRectangle(bar.panel, brush_);
	brush_->SetColor(theme_.accent);
	rt_->DrawRectangle(bar.panel, brush_, 1.0F);

	for (const MenuItemView& item : bar.items)
	{
		if (item.separator)
		{
			brush_->SetColor(theme_.gutter);
			rt_->FillRectangle(D2D1::RectF(item.rect.left + 4.0F, item.rect.top + 1.0F,
										   item.rect.right - 4.0F, item.rect.top + 2.0F),
							   brush_);
			continue;
		}
		if (item.hot && item.enabled)
		{
			brush_->SetColor(theme_.selection);
			rt_->FillRectangle(item.rect, brush_);
		}
		brush_->SetColor(item.enabled ? theme_.text : theme_.status_fg);
		// Подпись и акселератор: в метке они разделены табуляцией, а
		// сочетание клавиш принято выравнивать по правому краю.
		std::wstring name = item.label;
		std::wstring accel;
		const std::size_t tab = name.find(L'\t');
		if (tab != std::wstring::npos)
		{
			accel = name.substr(tab + 1);
			name.resize(tab);
		}
		if (!name.empty())
		{
			const D2D1_RECT_F text_rect = D2D1::RectF(item.rect.left + padding_, item.rect.top,
													  item.rect.right - padding_, item.rect.bottom);
			rt_->DrawTextW(name.c_str(), static_cast<UINT32>(name.size()), fmt_ui_, text_rect, brush_,
						   D2D1_DRAW_TEXT_OPTIONS_CLIP);
		}
		if (!accel.empty())
		{
			const float w = measure_ui(accel);
			brush_->SetColor(item.enabled ? theme_.status_fg : theme_.gutter);
			rt_->DrawTextW(accel.c_str(), static_cast<UINT32>(accel.size()), fmt_ui_,
						   D2D1::RectF(item.rect.right - padding_ - w - 6.0F, item.rect.top,
									   item.rect.right - padding_ - 6.0F, item.rect.bottom),
						   brush_, D2D1_DRAW_TEXT_OPTIONS_CLIP);
		}
		if (item.checked)
		{
			brush_->SetColor(theme_.accent);
			rt_->FillEllipse(D2D1::Ellipse(D2D1::Point2F(item.rect.left + 4.0F,
														 (item.rect.top + item.rect.bottom) * 0.5F),
										   2.0F, 2.0F),
							 brush_);
		}
	}
}

void Renderer::draw_ask(const AskView& view)
{
	if (rt_ == nullptr || fmt_ui_ == nullptr || brush_ == nullptr)
		return;

	brush_->SetColor(theme_.gutter);
	rt_->FillRectangle(view.panel, brush_);
	brush_->SetColor(theme_.accent);
	rt_->DrawRectangle(view.panel, brush_, 1.0F);

	for (const AskRow& row : view.rows)
	{
		if (row.text.empty())
			continue;
		const D2D1_COLOR_F base = row.kind == AskKind::accent   ? theme_.accent
								  : row.kind == AskKind::dim ? theme_.status_fg
															 : theme_.text;
		if (row.has_hot && row.hot_from < row.hot_to && row.hot_to <= row.text.size())
		{
			// Строка с обрабатываемым словом: обычный текст, слово — красным.
			const std::wstring prefix = row.text.substr(0, row.hot_from);
			const std::wstring word = row.text.substr(row.hot_from, row.hot_to - row.hot_from);
			const std::wstring suffix = row.text.substr(row.hot_to);
			const auto draw = [&](const std::wstring& part, float left, float right, const D2D1_COLOR_F& color)
			{
				if (part.empty() || right <= left)
					return;
				brush_->SetColor(color);
				rt_->DrawTextW(part.c_str(), static_cast<UINT32>(part.size()), fmt_ui_,
							   D2D1::RectF(left, row.rect.top, right, row.rect.bottom), brush_,
							   D2D1_DRAW_TEXT_OPTIONS_CLIP);
			};
			const float x0 = row.rect.left;
			const float x1 = x0 + measure_ui(prefix);
			const float x2 = x1 + measure_ui(word);
			draw(prefix, x0, x1, base);
			draw(word, x1, x2 + 1.0F, D2D1::ColorF(0.95F, 0.36F, 0.36F));
			draw(suffix, x2, row.rect.right, base);
			continue;
		}
		brush_->SetColor(base);
		rt_->DrawTextW(row.text.c_str(), static_cast<UINT32>(row.text.size()), fmt_ui_, row.rect,
					   brush_, D2D1_DRAW_TEXT_OPTIONS_CLIP);
	}

	for (const AskButtonView& b : view.buttons)
	{
		const bool lit = b.focus || b.hot;
		brush_->SetColor(lit ? theme_.selection : theme_.status_bg);
		rt_->FillRectangle(b.rect, brush_);
		brush_->SetColor(lit ? theme_.accent : theme_.status_fg);
		rt_->DrawRectangle(b.rect, brush_, 1.0F);
		brush_->SetColor(lit ? theme_.text : theme_.status_fg);
		rt_->DrawTextW(b.label.c_str(), static_cast<UINT32>(b.label.size()), fmt_ui_,
					   D2D1::RectF(b.rect.left + padding_ * 0.5F, b.rect.top,
								   b.rect.right - padding_ * 0.5F, b.rect.bottom),
					   brush_, D2D1_DRAW_TEXT_OPTIONS_CLIP);
	}
}

void Renderer::paint(Editor& ed, const std::wstring& status, bool show_find_bar,
					 const std::wstring& find_text, bool find_focus, bool caret,
					 const AskView* ask, const MenuBarView* bar)
{
	if (rt_ == nullptr && !create_render_target(hwnd_))
		return;
	if (brush_ == nullptr)
		return;

	rt_->BeginDraw();
	rt_->SetTransform(D2D1::Matrix3x2F::Identity());
	rt_->Clear(theme_.bg);

	const float text_h = std::max(
		0.0F, height_dip_ - bar_h_ - status_h_ - (show_find_bar ? find_h_ : 0.0F));
	const float text_y0 = bar_h_;
	ed.set_viewport(text_area_width(), text_h);

	const float scroll_x = ed.scroll_x();
	const float scroll_y = ed.scroll_y();
	const float lh = ed.metrics().line_height;
	const float cw = ed.metrics().char_width;

	const std::size_t first = ed.first_visible_line();
	const std::size_t count = ed.visible_line_count();

	// Полоса выделения рисуется под текстом.
	if (ed.has_selection())
	{
		const std::size_t sel_a = ed.sel_begin();
		const std::size_t sel_b = ed.sel_end();
		brush_->SetColor(theme_.selection);
		for (std::size_t i = first; i < first + count && i < ed.buffer().line_count(); ++i)
		{
			const std::size_t line_start = ed.buffer().line_start(i);
			const std::size_t line_len = ed.buffer().line_text(i).size();
			const std::size_t line_end = line_start + line_len;
			if (line_end < sel_a || line_start > sel_b)
				continue;
			const std::size_t from = sel_a > line_start ? sel_a - line_start : 0;
			const std::size_t to = std::min(sel_b - line_start, line_len);
			float x1 = padding_ + ed.prefix_width(i, from) - scroll_x;
			float x2 = padding_ + ed.prefix_width(i, to) - scroll_x;
			if (sel_b > line_end && to == line_len)
				x2 += cw * 0.5F; // выделение «через» перевод строки видно
			if (x2 < padding_ - scroll_x)
				x2 = padding_ - scroll_x;
			if (x1 > width_dip_)
				continue;
			const float y = text_y0 + ed.line_top(i) - scroll_y;
			rt_->FillRectangle(D2D1::RectF(std::max(x1, padding_ - scroll_x), y + 1.0F,
										   std::max(x2, x1 + 1.0F), y + lh - 1.0F),
							   brush_);
		}
	}

	rt_->PushAxisAlignedClip(D2D1::RectF(0.0F, text_y0, width_dip_, text_y0 + text_h),
							 D2D1_ANTIALIAS_MODE_ALIASED);

	// Текст видимых строк: раскладки кэшируются по номеру строки и тексту.
	for (CachedLine& c : cache_)
		c.used = false;
	brush_->SetColor(theme_.text);
	for (std::size_t i = first; i < first + count && i < ed.buffer().line_count(); ++i)
	{
		const std::string_view bytes = ed.buffer().line_text(i);
		if (bytes.empty())
			continue;
		const float line_y = text_y0 + ed.line_top(i) - scroll_y;
		if (line_y > text_h || line_y + lh < 0.0F)
			continue; // строка за пределами окна — раскладку не строим
		// Ширина текста для горизонтальной прокрутки уточняется по видимым
		// строкам: полный проход по гигабайтному файлу недопустим.
		ed.note_line_cells(ed.prefix_cells(i, bytes.size()));

		// Длинную строку раскладываем только видимым окном символов: иначе
		// DWrite формирует и растеризует всю строку при каждой перерисовке
		// (а курсор мигает дважды в секунду) — окно «замирает» на длинных
		// строках, что и выглядело как зависание после вставки.
		std::size_t from_byte = 0;
		std::wstring wide;
		if (bytes.size() <= kLineSliceBytes)
		{
			wide = utf8_to_wide(bytes);
		}
		else
		{
			const float margin = kSliceMarginCells * std::max(1.0F, cw);
			const float doc_y = ed.line_top(i) + 1.0F;
			const std::size_t b0 = ed.offset_at(std::max(0.0F, scroll_x - margin), doc_y);
			const std::size_t b1 = ed.offset_at(scroll_x + text_area_width() + margin, doc_y);
			from_byte = std::min(b0, bytes.size());
			// Срез может начинаться посреди символа — сдвигаем к его началу.
			while (from_byte < bytes.size() && (static_cast<unsigned char>(bytes[from_byte]) & 0xC0U) == 0x80U)
				++from_byte;
			const std::size_t to_byte = std::min(std::max(b1, from_byte), bytes.size());
			wide = utf8_to_wide(bytes.substr(from_byte, to_byte - from_byte));
		}
		if (wide.empty())
			continue;

		CachedLine* slot = nullptr;
		for (CachedLine& c : cache_)
		{
			if (!c.used && c.line == i && c.from == from_byte && c.text == wide)
			{
				slot = &c;
				break;
			}
		}
		if (slot == nullptr)
		{
			IDWriteTextLayout* layout = nullptr;
			if (FAILED(dwrite_->CreateTextLayout(wide.c_str(), static_cast<UINT32>(wide.size()), fmt_,
												 1.0e6F, line_h_ * 2.0F, &layout)))
				continue;
			// Подсветка ё (как метка ёфикации в оригинале): акцентный цвет
			// на каждый символ «ё»/«Ё» через drawing effect.
			if (accent_brush_ != nullptr)
			{
				for (UINT32 p = 0; p < static_cast<UINT32>(wide.size()); ++p)
				{
					if (wide[p] == L'ё' || wide[p] == L'Ё')
						layout->SetDrawingEffect(accent_brush_, DWRITE_TEXT_RANGE{ p, 1 });
				}
			}
			// Ищем свободный слот: неиспользованный с другим текстом.
			for (CachedLine& c : cache_)
			{
				if (!c.used)
				{
					if (c.layout != nullptr)
						c.layout->Release();
					c.layout = layout;
					c.line = i;
					c.from = from_byte;
					c.text = std::move(wide);
					c.used = true;
					slot = &c;
					break;
				}
			}
			if (slot == nullptr)
			{
				CachedLine c;
				c.layout = layout;
				c.line = i;
				c.from = from_byte;
				c.text = std::move(wide);
				c.used = true;
				cache_.push_back(std::move(c));
				slot = &cache_.back();
			}
		}
		else
		{
			slot->used = true;
		}

		if (slot->layout == nullptr)
			continue;
		// Срез рисуется со сдвигом на ширину отброшенного начала строки.
		const float slice_x = ed.prefix_width(i, from_byte);
		rt_->DrawTextLayout(D2D1::Point2F(padding_ + slice_x - scroll_x, line_y), slot->layout, brush_,
							D2D1_DRAW_TEXT_OPTIONS_CLIP);
	}
	// Лишние раскладки выбрасываем, чтобы кэш не рос.
	for (auto it = cache_.begin(); it != cache_.end();)
	{
		if (!it->used)
		{
			if (it->layout != nullptr)
				it->layout->Release();
			it = cache_.erase(it);
		}
		else
		{
			++it;
		}
	}

	// Курсор.
	if (caret)
	{
		const float x = padding_ + ed.caret_x() - scroll_x;
		const float y = text_y0 + ed.caret_y() - scroll_y;
		brush_->SetColor(theme_.caret);
		rt_->FillRectangle(D2D1::RectF(x, y + 1.0F, x + std::max(1.0F, dpi_scale_), y + lh - 1.0F), brush_);
	}

	rt_->PopAxisAlignedClip();

	// Панель вопросов поверх затемнённого документа: видно, что ввод ждут.
	if (ask != nullptr && dim_brush_ != nullptr)
		rt_->FillRectangle(D2D1::RectF(0.0F, text_y0, width_dip_, text_y0 + text_h), dim_brush_);

	// Слово, про которое спрашивает ёфикация, подсвечиваем в тексте — как в
	// AskPanel оригинала: видно, к чему относится вопрос.
	if (ask != nullptr && ask->has_word)
		draw_word_highlight(ed, scroll_x, scroll_y, text_h, ask->word_from, ask->word_to);

	if (show_find_bar)
		draw_find_bar(find_text, find_focus);
	draw_status(status);
	if (ask != nullptr)
		draw_ask(*ask);
	if (bar != nullptr)
		draw_menu_bar(*bar);

	const HRESULT hr = rt_->EndDraw();
	if (hr == D2DERR_RECREATE_TARGET)
	{
		if (rt_ != nullptr)
		{
			rt_->Release();
			rt_ = nullptr;
		}
		if (brush_ != nullptr)
		{
			brush_->Release();
			brush_ = nullptr;
		}
	}
}

} // namespace yo::gui
