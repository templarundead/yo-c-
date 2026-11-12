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
	if (mark_brush_ != nullptr)
	{
		mark_brush_->Release();
		mark_brush_ = nullptr;
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
	if (fmt_center_ != nullptr)
	{
		fmt_center_->Release();
		fmt_center_ = nullptr;
	}
	if (fmt_ask_ != nullptr)
	{
		fmt_ask_->Release();
		fmt_ask_ = nullptr;
	}
	if (fmt_right_ != nullptr)
	{
		fmt_right_->Release();
		fmt_right_ = nullptr;
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
	// Панель вопросов, меню и окно настроек — фиксированный «интерфейсный»
	// кегль: размер шрифта текста (FontSize, Ctrl+±, диалог «Шрифт») на них
	// не влияет вообще.
	ask_font_size_ = 15.0F;
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
	if (fmt_center_ != nullptr)
	{
		fmt_center_->Release();
		fmt_center_ = nullptr;
	}
	if (fmt_ask_ != nullptr)
	{
		fmt_ask_->Release();
		fmt_ask_ = nullptr;
	}
	if (fmt_right_ != nullptr)
	{
		fmt_right_->Release();
		fmt_right_ = nullptr;
	}
	// Моноширинный шрифт: выбор пользователя (диалог «Шрифт», как TFontDialog
	// оригинала) важнее встроенного набора; иначе Cascadia Mono (Windows 11/10),
	// Consolas и Courier New — DirectWrite подберёт ближайший доступный.
	const wchar_t* families[] = { L"Cascadia Mono", L"Consolas", L"Courier New" };
	const float size = font_size_;
	if (!font_family_.empty())
	{
		if (FAILED(dwrite_->CreateTextFormat(font_family_.c_str(), nullptr, DWRITE_FONT_WEIGHT_NORMAL,
											 DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, size,
											 L"ru-ru", &fmt_)))
			fmt_ = nullptr;
	}
	for (const wchar_t* family : families)
	{
		if (fmt_ != nullptr)
			break;
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
	// Номера строк — тем же размером, по правому краю поля.
	if (FAILED(dwrite_->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_NORMAL,
										DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 12.0F,
										L"ru-ru", &fmt_right_)))
		return false;
	fmt_right_->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
	fmt_right_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_TRAILING);
	fmt_right_->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

	// Панель вопросов: подписи и строки по центру, как в оригинале. Шрифт
	// панели — не меньше, чем текст окна по умолчанию (15 DIP), и не зависит
	// от Ctrl+±, иначе панель «прыгала» вместе с зумом.
	const float ask_font = ask_font_size_;
	if (FAILED(dwrite_->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_NORMAL,
										DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, ask_font,
										L"ru-ru", &fmt_center_)))
		return false;
	fmt_center_->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
	fmt_center_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
	fmt_center_->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
	// Тот же размер, но с выравниванием по левому краю: так рисуется строка
	// с подсвеченным словом (куски ставятся по координатам).
	if (FAILED(dwrite_->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_NORMAL,
										DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, ask_font,
										L"ru-ru", &fmt_ask_)))
		return false;
	fmt_ask_->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
	fmt_ask_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
	fmt_ask_->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

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

	// Метрики шрифта интерфейса (меню, статус) и шрифта панели вопросов:
	// они не должны меняться вместе с размером текста.
	const auto measure_ui_format = [&](IDWriteTextFormat* format, float& char_w, float& line_h)
	{
		IDWriteTextLayout* layout = nullptr;
		if (FAILED(dwrite_->CreateTextLayout(probe.c_str(), static_cast<UINT32>(probe.size()), format,
											4096.0F, 4096.0F, &layout)))
			return;
		DWRITE_TEXT_METRICS um{};
		if (SUCCEEDED(layout->GetMetrics(&um)))
		{
			char_w = std::max(1.0F, um.widthIncludingTrailingWhitespace / 64.0F);
			line_h = std::max(1.0F, std::ceil(um.height));
		}
		layout->Release();
	};
	measure_ui_format(fmt_ui_, ui_char_w_, ui_line_h_);
	measure_ui_format(fmt_center_, ask_char_w_, ask_line_h_);

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
	return set_editor_font(font_family_, size_dip, hwnd);
}

bool Renderer::set_editor_font(std::wstring_view family, float size_dip, HWND hwnd)
{
	font_family_.assign(family);
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
	if (mark_brush_ != nullptr)
	{
		mark_brush_->Release();
		mark_brush_ = nullptr;
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
	if (FAILED(rt_->CreateSolidColorBrush(yofy_text_color_, &mark_brush_)))
		mark_brush_ = nullptr;
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
	return std::max(0.0F, width_dip_ - 2.0F * padding_ - gutter_w_);
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

void Renderer::draw_find_bar(const FindBarView& view)
{
	if (rt_ == nullptr || fmt_ui_ == nullptr || brush_ == nullptr)
		return;
	brush_->SetColor(theme_.gutter);
	rt_->FillRectangle(view.rect, brush_);
	brush_->SetColor(D2D1::ColorF(0.05F, 0.05F, 0.06F));
	rt_->FillRectangle(D2D1::RectF(view.rect.left, view.rect.top, view.rect.right, view.rect.top + 1.0F),
					   brush_);

	const auto draw_field = [&](const D2D1_RECT_F& rect, const std::wstring& value, bool active, bool replace_field)
	{
		brush_->SetColor(active ? theme_.accent : theme_.status_fg);
		rt_->DrawRectangle(rect, brush_, 1.0F);
		if (!value.empty())
		{
			brush_->SetColor(theme_.text);
			rt_->DrawTextW(value.c_str(), static_cast<UINT32>(value.size()), fmt_ui_,
						   D2D1::RectF(rect.left + 4.0F, rect.top, rect.right - 4.0F, rect.bottom), brush_,
						   D2D1_DRAW_TEXT_OPTIONS_CLIP);
		}
		else
		{
			// Подсказка в пустом поле — чтобы было понятно, куда вводить.
			brush_->SetColor(theme_.status_fg);
			const std::wstring hint = replace_field ? L"на что заменить" : L"что найти";
			rt_->DrawTextW(hint.c_str(), static_cast<UINT32>(hint.size()), fmt_ui_,
						   D2D1::RectF(rect.left + 4.0F, rect.top, rect.right - 4.0F, rect.bottom), brush_,
						   D2D1_DRAW_TEXT_OPTIONS_CLIP);
		}
		if (active)
		{
			// Курсор ввода: видно, куда попадёт набранное.
			brush_->SetColor(theme_.text);
			const float x = rect.left + 4.0F + measure_ui(value);
			rt_->FillRectangle(D2D1::RectF(x, rect.top + 2.0F, x + std::max(1.0F, dpi_scale_),
										  rect.bottom - 2.0F),
							   brush_);
		}
	};

	draw_field(view.query_rect, view.query, view.focus == 0, false);
	if (view.replace_mode)
		draw_field(view.replace_rect, view.replace, view.focus == 1, true);

	for (const FindBarButtonView& b : view.buttons)
	{
		const bool lit = b.hot || (b.toggle && b.checked);
		brush_->SetColor(lit ? theme_.selection : theme_.status_bg);
		rt_->FillRectangle(b.rect, brush_);
		brush_->SetColor(lit ? theme_.accent : theme_.gutter);
		rt_->DrawRectangle(b.rect, brush_, 1.0F);
		brush_->SetColor(lit ? theme_.text : theme_.status_fg);
		rt_->DrawTextW(b.label.c_str(), static_cast<UINT32>(b.label.size()), fmt_center_, b.rect, brush_,
					   D2D1_DRAW_TEXT_OPTIONS_CLIP);
	}

}

// Подсветка найденных совпадений: жёлтая заливка под текстом.
void Renderer::draw_marks(Editor& ed, float scroll_x, float scroll_y, float text_h,
						  const std::vector<std::pair<std::size_t, std::size_t>>& marks, D2D1_COLOR_F color,
						  float height_fraction)
{
	if (rt_ == nullptr || brush_ == nullptr || marks.empty())
		return;
	const float lh = std::max(1.0F, ed.metrics().line_height);
	brush_->SetColor(color);
	// Ряды: без переноса ряд совпадает с логической строкой, с переносом
	// подсветка режется по видимым рядам.
	for (const Editor::Row& row : ed.visible_rows())
	{
		const std::size_t line_start = ed.buffer().line_start(row.line);
		const std::size_t line_len = ed.buffer().line_text(row.line).size();
		const std::size_t row_start = line_start + row.from;
		const std::size_t row_end = line_start + std::min(row.to, line_len);
		const float y = bar_h_ + row.top - scroll_y;
		if (y + lh < bar_h_ || y > bar_h_ + text_h)
			continue;
		for (const auto& [from, to] : marks)
		{
			if (to <= row_start || from >= row_end)
				continue;
			if (row_end <= row_start)
				continue;
			const std::size_t a = from > row_start ? from - line_start : row.from;
			const std::size_t b = std::min(to - line_start, row.to);
			if (b <= a)
				continue;
			const float x1 = text_x0() + ed.row_width(row, a) - scroll_x;
			const float x2 = text_x0() + ed.row_width(row, b) - scroll_x;
			// height_fraction < 1 — тонкая линия у нижнего края ряда (цвет
			// пометки), 1 — фон всей высоты.
			const float top = height_fraction >= 1.0F ? y + 1.0F
													  : y + lh * (1.0F - height_fraction) - 1.0F;
			const float bottom = height_fraction >= 1.0F ? y + lh - 1.0F : y + lh - 1.0F;
			rt_->FillRectangle(D2D1::RectF(x1, top, std::max(x2, x1 + 1.0F), bottom), brush_);
		}
	}
}

void Renderer::draw_options(const OptionsView& view)
{
	if (rt_ == nullptr || fmt_ui_ == nullptr || fmt_center_ == nullptr || brush_ == nullptr)
		return;

	// Затемнение документа под окном: видно, что ввод ждут здесь.
	if (dim_brush_ != nullptr)
		rt_->FillRectangle(D2D1::RectF(0.0F, 0.0F, width_dip_, height_dip_), dim_brush_);

	brush_->SetColor(theme_.gutter);
	rt_->FillRectangle(view.rect, brush_);
	brush_->SetColor(theme_.accent);
	rt_->DrawRectangle(view.rect, brush_, 1.0F);

	// Заголовок.
	brush_->SetColor(theme_.text);
	rt_->DrawTextW(view.title.c_str(), static_cast<UINT32>(view.title.size()), fmt_ui_,
				   D2D1::RectF(view.rect.left + 14.0F, view.rect.top + 8.0F, view.rect.right - 14.0F,
							   view.rect.top + 28.0F),
				   brush_, D2D1_DRAW_TEXT_OPTIONS_CLIP);

	for (std::size_t i = 0; i < view.rows.size(); ++i)
	{
		const OptionsRowView& row = view.rows[i];
		if (row.tab)
		{
			// Заголовок вкладки: активная — залита цветом выделения и
			// подчёркнута акцентом, как ярлык TPageControl оригинала.
			brush_->SetColor(row.tab_active ? theme_.selection
											: (static_cast<int>(i) == view.focus ? theme_.status_bg
																				: theme_.gutter));
			rt_->FillRectangle(row.rect, brush_);
			brush_->SetColor(row.tab_active ? theme_.accent : theme_.status_fg);
			rt_->DrawRectangle(row.rect, brush_, 1.0F);
			brush_->SetColor(row.tab_active ? theme_.text : theme_.status_fg);
			rt_->DrawTextW(row.label.c_str(), static_cast<UINT32>(row.label.size()), fmt_center_,
						   row.rect, brush_, D2D1_DRAW_TEXT_OPTIONS_CLIP);
			continue;
		}
		if (static_cast<int>(i) == view.focus)
		{
			brush_->SetColor(theme_.selection);
			rt_->FillRectangle(row.rect, brush_);
		}
		float x = row.rect.left + 2.0F;
		if (row.check)
		{
			// Галочка: рамка и «×» внутри — тот же вид, что у кнопок панели.
			const float box = std::max(12.0F, row.rect.bottom - row.rect.top - 8.0F);
			const float top = row.rect.top + (row.rect.bottom - row.rect.top - box) * 0.5F;
			const D2D1_RECT_F mark_rect = D2D1::RectF(x, top, x + box, top + box);
			brush_->SetColor(theme_.status_fg);
			rt_->DrawRectangle(mark_rect, brush_, 1.0F);
			if (row.checked)
			{
				const std::wstring tick = L"×";
				brush_->SetColor(theme_.text);
				rt_->DrawTextW(tick.c_str(), static_cast<UINT32>(tick.size()), fmt_center_, mark_rect,
							   brush_, D2D1_DRAW_TEXT_OPTIONS_CLIP);
			}
			x += box + 8.0F;
		}
		// Недоступная строка (правила OptionsPage.cbClick) — приглушённая.
		brush_->SetColor(row.enabled ? (row.path ? theme_.accent : theme_.text) : theme_.status_fg);
		const float value_w = row.value.empty() ? 0.0F : measure_ui(row.value) + 10.0F;
		rt_->DrawTextW(row.label.c_str(), static_cast<UINT32>(row.label.size()), fmt_ui_,
					   D2D1::RectF(x, row.rect.top, std::max(x + 10.0F, row.rect.right - value_w),
								   row.rect.bottom),
					   brush_, D2D1_DRAW_TEXT_OPTIONS_CLIP);
		if (!row.value.empty())
		{
			brush_->SetColor(row.enabled ? (row.path ? theme_.status_fg : theme_.text) : theme_.status_fg);
			rt_->DrawTextW(row.value.c_str(), static_cast<UINT32>(row.value.size()), fmt_ui_,
						   D2D1::RectF(row.rect.right - value_w, row.rect.top, row.rect.right,
									   row.rect.bottom),
						   brush_, D2D1_DRAW_TEXT_OPTIONS_CLIP);
		}
	}

	for (std::size_t i = 0; i < view.buttons.size(); ++i)
	{
		const OptionsButtonView& b = view.buttons[i];
		const bool lit = static_cast<int>(i) == view.hot || b.primary;
		brush_->SetColor(lit ? theme_.selection : theme_.status_bg);
		rt_->FillRectangle(b.rect, brush_);
		brush_->SetColor(lit ? theme_.accent : theme_.status_fg);
		rt_->DrawRectangle(b.rect, brush_, 1.0F);
		brush_->SetColor(theme_.text);
		rt_->DrawTextW(b.label.c_str(), static_cast<UINT32>(b.label.size()), fmt_center_, b.rect, brush_,
					   D2D1_DRAW_TEXT_OPTIONS_CLIP);
	}
}

float Renderer::measure_ask(const std::wstring& text)
{
	if (dwrite_ == nullptr || fmt_ask_ == nullptr || text.empty())
		return 0.0F;
	IDWriteTextLayout* layout = nullptr;
	if (FAILED(dwrite_->CreateTextLayout(text.c_str(), static_cast<UINT32>(text.size()), fmt_ask_, 4096.0F,
										4096.0F, &layout)))
		return 0.0F;
	DWRITE_TEXT_METRICS tm{};
	const HRESULT hr = layout->GetMetrics(&tm);
	layout->Release();
	return SUCCEEDED(hr) ? tm.widthIncludingTrailingWhitespace : 0.0F;
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
	// Полупрозрачная заливка и рамка: буквы остаются читаемыми, а слово
	// видно и поверх затемнения панели вопросов (раньше заливка была почти
	// непрозрачной и «съедала» само слово).
	const D2D1_COLOR_F fill = D2D1::ColorF(0.72F, 0.16F, 0.16F, 0.38F);
	const D2D1_COLOR_F frame = D2D1::ColorF(0.95F, 0.38F, 0.30F, 0.95F);
	for (const Editor::Row& row : ed.visible_rows())
	{
		const std::size_t line_start = ed.buffer().line_start(row.line);
		const std::size_t line_len = ed.buffer().line_text(row.line).size();
		const std::size_t row_start = line_start + row.from;
		const std::size_t row_end = line_start + std::min(row.to, line_len);
		if (row_end <= row_start || row_end < from || row_start > to)
			continue;
		const std::size_t a = from > row_start ? from - line_start : row.from;
		const std::size_t b = std::min(to - line_start, row.to);
		if (b <= a)
			continue;
		const float x1 = text_x0() + ed.row_width(row, a) - scroll_x;
		const float x2 = text_x0() + ed.row_width(row, b) - scroll_x;
		const float y = bar_h_ + row.top - scroll_y;
		if (y + lh < bar_h_ || y > bar_h_ + text_h)
			continue;
		const D2D1_RECT_F rect = D2D1::RectF(x1, y + 1.0F, std::max(x2, x1 + 1.0F), y + lh - 1.0F);
		brush_->SetColor(fill);
		rt_->FillRectangle(rect, brush_);
		brush_->SetColor(frame);
		rt_->DrawRectangle(rect, brush_, 1.5F);
	}
}

// Редактор словарей: таблица, поля ввода, кнопки и строка сообщения.
void Renderer::draw_dict_editor(const DictView& view)
{
	if (rt_ == nullptr || fmt_ui_ == nullptr || fmt_center_ == nullptr || brush_ == nullptr)
		return;

	if (dim_brush_ != nullptr)
		rt_->FillRectangle(D2D1::RectF(0.0F, 0.0F, width_dip_, height_dip_), dim_brush_);

	brush_->SetColor(theme_.gutter);
	rt_->FillRectangle(view.rect, brush_);
	brush_->SetColor(theme_.accent);
	rt_->DrawRectangle(view.rect, brush_, 1.0F);

	// Заголовок и подпись («словарь: N слов»).
	brush_->SetColor(theme_.text);
	rt_->DrawTextW(view.title.c_str(), static_cast<UINT32>(view.title.size()), fmt_ui_,
				   D2D1::RectF(view.rect.left + 14.0F, view.rect.top + 6.0F, view.rect.right - 14.0F,
							   view.rect.top + 28.0F),
				   brush_, D2D1_DRAW_TEXT_OPTIONS_CLIP);
	brush_->SetColor(theme_.status_fg);
	rt_->DrawTextW(view.subtitle.c_str(), static_cast<UINT32>(view.subtitle.size()), fmt_ui_,
				   D2D1::RectF(view.rect.left + 14.0F, view.rect.top + 26.0F, view.rect.right - 14.0F,
							   view.rect.top + 46.0F),
				   brush_, D2D1_DRAW_TEXT_OPTIONS_CLIP);

	// Поля ввода: фильтр и слово/правило. Рамка — акцентная у поля с фокусом.
	const auto draw_field = [this](const D2D1_RECT_F& rect, const std::wstring& text, bool focus,
								   const std::wstring& empty_hint)
	{
		brush_->SetColor(theme_.bg);
		rt_->FillRectangle(rect, brush_);
		brush_->SetColor(focus ? theme_.accent : theme_.status_fg);
		rt_->DrawRectangle(rect, brush_, 1.0F);
		const std::wstring& shown = text.empty() ? empty_hint : text;
		brush_->SetColor(text.empty() ? theme_.status_fg : theme_.text);
		rt_->DrawTextW(shown.c_str(), static_cast<UINT32>(shown.size()), fmt_ui_,
					   D2D1::RectF(rect.left + 6.0F, rect.top, rect.right - 6.0F, rect.bottom), brush_,
					   D2D1_DRAW_TEXT_OPTIONS_CLIP);
		if (focus)
		{
			// Курсор поля — черта в конце текста (ввод всегда в конец).
			const float w = text.empty() ? 0.0F : measure_ui(text);
			const float x = std::min(rect.left + 6.0F + w, rect.right - 3.0F);
			brush_->SetColor(theme_.caret);
			rt_->FillRectangle(D2D1::RectF(x, rect.top + 3.0F, x + 1.0F, rect.bottom - 3.0F),
							   brush_);
		}
	};

	draw_field(view.filter_rect, view.filter, view.filter_focus, L"фильтр…");
	if (!view.edit_label.empty())
	{
		brush_->SetColor(theme_.status_fg);
		rt_->DrawTextW(view.edit_label.c_str(), static_cast<UINT32>(view.edit_label.size()), fmt_ui_,
					   D2D1::RectF(view.edit_rect.left - 0.0F, view.edit_rect.top - 18.0F,
								   view.edit_rect.right, view.edit_rect.top - 2.0F),
					   brush_, D2D1_DRAW_TEXT_OPTIONS_CLIP);
	}
	draw_field(view.edit_rect, view.edit, view.edit_focus,
			   view.edit_label.empty() ? L"правило…" : L"слово с «ё»…");

	// Строки таблицы.
	for (const DictRowView& row : view.rows)
	{
		if (row.current)
		{
			brush_->SetColor(theme_.selection);
			rt_->FillRectangle(row.rect, brush_);
		}
		brush_->SetColor(row.error ? D2D1::ColorF(0.85F, 0.35F, 0.30F) : theme_.text);
		rt_->DrawTextW(row.text.c_str(), static_cast<UINT32>(row.text.size()), fmt_ui_,
					   D2D1::RectF(row.rect.left + 6.0F, row.rect.top, row.rect.right - 6.0F,
								   row.rect.bottom),
					   brush_, D2D1_DRAW_TEXT_OPTIONS_CLIP);
	}

	for (const DictButtonView& b : view.buttons)
	{
		D2D1_COLOR_F fill = theme_.status_bg;
		D2D1_COLOR_F border = theme_.status_fg;
		if (b.toggle && b.checked)
		{
			fill = theme_.selection;
			border = theme_.accent;
		}
		if (b.hot || b.primary)
		{
			fill = theme_.selection;
			border = theme_.accent;
		}
		brush_->SetColor(fill);
		rt_->FillRectangle(b.rect, brush_);
		brush_->SetColor(border);
		rt_->DrawRectangle(b.rect, brush_, 1.0F);
		brush_->SetColor(theme_.text);
		rt_->DrawTextW(b.label.c_str(), static_cast<UINT32>(b.label.size()), fmt_center_, b.rect,
					   brush_, D2D1_DRAW_TEXT_OPTIONS_CLIP);
	}

	// Сообщение (ошибки — красным) и подсказка по клавишам.
	brush_->SetColor(view.message_error ? D2D1::ColorF(0.95F, 0.45F, 0.40F) : theme_.status_fg);
	rt_->DrawTextW(view.message.c_str(), static_cast<UINT32>(view.message.size()), fmt_ui_,
				   view.message_rect, brush_, D2D1_DRAW_TEXT_OPTIONS_CLIP);
	brush_->SetColor(theme_.status_fg);
	rt_->DrawTextW(view.hint.c_str(), static_cast<UINT32>(view.hint.size()), fmt_ui_,
				   D2D1::RectF(view.rect.left + 14.0F, view.rect.bottom - 22.0F,
							   view.rect.right - 14.0F, view.rect.bottom - 4.0F),
				   brush_, D2D1_DRAW_TEXT_OPTIONS_CLIP);
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
	if (rt_ == nullptr || fmt_ui_ == nullptr || fmt_center_ == nullptr || brush_ == nullptr)
		return;

	// Фон полосы и линия сверху: панель идёт во всю ширину окна, как
	// AskPanel оригинала.
	brush_->SetColor(theme_.gutter);
	rt_->FillRectangle(view.panel, brush_);
	brush_->SetColor(theme_.accent);
	rt_->FillRectangle(D2D1::RectF(view.panel.left, view.panel.top, view.panel.right,
								   view.panel.top + 1.0F),
					   brush_);

	for (const AskRow& row : view.rows)
	{
		if (row.text.empty())
			continue;
		const D2D1_COLOR_F base = row.kind == AskKind::accent   ? theme_.accent
								  : row.kind == AskKind::dim ? theme_.status_fg
															 : theme_.text;
		const bool hot = row.has_hot && row.hot_from < row.hot_to && row.hot_to <= row.text.size();
		if (!hot)
		{
			brush_->SetColor(base);
			rt_->DrawTextW(row.text.c_str(), static_cast<UINT32>(row.text.size()), fmt_center_,
						   row.rect, brush_, D2D1_DRAW_TEXT_OPTIONS_CLIP);
			continue;
		}

		// Строка с обрабатываемым словом: слово стоит ровно по центру полосы,
		// остальное предложение уходит под край и подрезается (переносов нет).
		const std::wstring prefix = row.text.substr(0, row.hot_from);
		const std::wstring word = row.text.substr(row.hot_from, row.hot_to - row.hot_from);
		const std::wstring suffix = row.text.substr(row.hot_to);
		const float w_prefix = measure_ask(prefix);
		const float w_word = measure_ask(word);
		const float mid = (row.rect.left + row.rect.right) * 0.5F;
		const float x0 = mid - (w_prefix + w_word * 0.5F);
		rt_->PushAxisAlignedClip(row.rect, D2D1_ANTIALIAS_MODE_ALIASED);
		const auto draw = [&](const std::wstring& part, float left, const D2D1_COLOR_F& color)
		{
			if (part.empty())
				return;
			brush_->SetColor(color);
			rt_->DrawTextW(part.c_str(), static_cast<UINT32>(part.size()), fmt_ask_,
						   D2D1::RectF(left, row.rect.top, row.rect.right, row.rect.bottom), brush_,
						   D2D1_DRAW_TEXT_OPTIONS_CLIP);
		};
		draw(prefix, x0, base);
		draw(word, x0 + w_prefix, D2D1::ColorF(0.95F, 0.36F, 0.36F));
		draw(suffix, x0 + w_prefix + w_word, base);
		rt_->PopAxisAlignedClip();
	}

	for (const AskButtonView& b : view.buttons)
	{
		const bool lit = b.focus || b.hot;
		// Системные тона: без «синих рамок». «Е» — синяя кнопка, «Ё» —
		// зелёная, остальные — как обычные кнопки окна.
		D2D1_COLOR_F fill = theme_.status_bg;
		D2D1_COLOR_F border = theme_.gutter;
		D2D1_COLOR_F text = theme_.text;
		switch (b.tone)
		{
		case AskTone::ye:
			fill = lit ? D2D1::ColorF(0.18F, 0.34F, 0.55F) : D2D1::ColorF(0.14F, 0.24F, 0.38F);
			border = D2D1::ColorF(0.28F, 0.44F, 0.64F);
			break;
		case AskTone::yo:
			fill = lit ? D2D1::ColorF(0.16F, 0.40F, 0.24F) : D2D1::ColorF(0.12F, 0.28F, 0.18F);
			border = D2D1::ColorF(0.26F, 0.50F, 0.32F);
			break;
		case AskTone::neutral:
		default:
			if (lit)
			{
				fill = theme_.selection;
				border = theme_.accent;
			}
			break;
		}
		brush_->SetColor(fill);
		rt_->FillRectangle(b.rect, brush_);
		brush_->SetColor(border);
		rt_->DrawRectangle(b.rect, brush_, 1.0F);
		brush_->SetColor(text);
		rt_->DrawTextW(b.label.c_str(), static_cast<UINT32>(b.label.size()), fmt_center_,
					   D2D1::RectF(b.rect.left + padding_ * 0.5F, b.rect.top,
								   b.rect.right - padding_ * 0.5F, b.rect.bottom),
					   brush_, D2D1_DRAW_TEXT_OPTIONS_CLIP);
	}
}

void Renderer::paint(Editor& ed, const std::wstring& status, const FindBarView* find, bool caret,
					 const AskView* ask, const MenuBarView* bar,
					 const std::vector<std::pair<std::size_t, std::size_t>>& marks,
					 const std::vector<std::pair<std::size_t, std::size_t>>& yofy_marks,
					 const OptionsView* options, const DictView* dict,
				 const std::pair<std::size_t, std::size_t>& word_highlight)
{
	if (rt_ == nullptr && !create_render_target(hwnd_))
		return;
	if (brush_ == nullptr)
		return;

	rt_->BeginDraw();
	rt_->SetTransform(D2D1::Matrix3x2F::Identity());
	rt_->Clear(theme_.bg);

	// Полоса вопросов внизу отнимает место у текста, как и строка поиска.
	const float ask_inset = ask != nullptr ? ask->bottom_inset : 0.0F;
	const float find_inset = find != nullptr ? find->h : 0.0F;
	const float text_h = std::max(
		0.0F, height_dip_ - bar_h_ - status_h_ - find_inset - ask_inset);
	const float text_y0 = bar_h_;
	ed.set_viewport(text_area_width(), text_h);

	const float scroll_x = ed.scroll_x();
	const float scroll_y = ed.scroll_y();
	const float lh = ed.metrics().line_height;
	const float cw = ed.metrics().char_width;

	// Видимые ряды: без переноса ряд = логическая строка, с переносом
	// (WordWrap) одна строка занимает несколько рядов.
	const std::vector<Editor::Row> rows = ed.visible_rows();

	// Подсветка ёфикационных замен (как Mark оригинала) и найденного —
	// под выделением и под текстом.
	// Фон пометки ([Colors] ColorBackMark) — под текстом; сам текст красится
	// ниже, drawing-эффектом раскладки (см. цикл отрисовки рядов).
	draw_marks(ed, scroll_x, scroll_y, text_h, yofy_marks, yofy_back_color_);
	if (mark_brush_ != nullptr)
		mark_brush_->SetColor(yofy_text_color_);
	draw_marks(ed, scroll_x, scroll_y, text_h, marks, D2D1::ColorF(0.95F, 0.80F, 0.20F, 0.35F));

	// Полоса выделения рисуется под текстом.
	if (ed.has_selection())
	{
		const std::size_t sel_a = ed.sel_begin();
		const std::size_t sel_b = ed.sel_end();
		brush_->SetColor(theme_.selection);
		for (const Editor::Row& row : rows)
		{
			const std::size_t line_start = ed.buffer().line_start(row.line);
			const std::size_t line_len = ed.buffer().line_text(row.line).size();
			const std::size_t line_end = line_start + line_len;
			const std::size_t row_start = line_start + row.from;
			const std::size_t row_end = line_start + std::min(row.to, line_len);
			if (row_end < sel_a || row_start > sel_b)
				continue;
			const std::size_t from = sel_a > row_start ? sel_a - line_start : row.from;
			const std::size_t to = std::min(sel_b - line_start, row.to);
			float x1 = text_x0() + ed.row_width(row, from) - scroll_x;
			float x2 = text_x0() + ed.row_width(row, to) - scroll_x;
			if (sel_b > line_end && to == line_len && row.last_of_line)
				x2 += cw * 0.5F; // выделение «через» перевод строки видно
			if (x2 < text_x0() - scroll_x)
				x2 = text_x0() - scroll_x;
			if (x1 > width_dip_)
				continue;
			const float y = text_y0 + row.top - scroll_y;
			rt_->FillRectangle(D2D1::RectF(std::max(x1, text_x0() - scroll_x), y + 1.0F,
										   std::max(x2, x1 + 1.0F), y + lh - 1.0F),
							   brush_);
		}
	}

	rt_->PushAxisAlignedClip(D2D1::RectF(0.0F, text_y0, width_dip_, text_y0 + text_h),
							 D2D1_ANTIALIAS_MODE_ALIASED);

	// Номера строк: поле слева, выключено по умолчанию (Ctrl+L в меню «Вид»).
	if (gutter_w_ > 0.0F && fmt_ui_ != nullptr)
	{
		brush_->SetColor(theme_.status_fg);
		for (const Editor::Row& row : rows)
		{
			if (!row.first_of_line)
				continue; // при переносе номер — только у первого ряда
			const float line_y = text_y0 + row.top - scroll_y;
			if (line_y > text_y0 + text_h || line_y + lh < text_y0)
				continue;
			const std::wstring num = std::to_wstring(row.line + 1);
			rt_->DrawTextW(num.c_str(), static_cast<UINT32>(num.size()), fmt_right_, 
						   D2D1::RectF(0.0F, line_y, text_x0() - 4.0F, line_y + lh), brush_,
						   D2D1_DRAW_TEXT_OPTIONS_CLIP);
		}
	}

	// Текст видимых строк: раскладки кэшируются по номеру строки и тексту.
	for (CachedLine& c : cache_)
		c.used = false;
	brush_->SetColor(theme_.text);
	for (const Editor::Row& row : rows)
	{
		const std::size_t i = row.line;
		const std::string_view bytes = ed.buffer().line_text(i);
		if (bytes.empty() || row.to <= row.from)
			continue;
		const float line_y = text_y0 + row.top - scroll_y;
		if (line_y > text_h || line_y + lh < 0.0F)
			continue; // строка за пределами окна — раскладку не строим
		// Ширина текста для горизонтальной прокрутки уточняется по видимым
		// строкам: полный проход по гигабайтному файлу недопустим.
		if (!ed.word_wrap())
			ed.note_line_cells(ed.prefix_cells(i, bytes.size()));

		// Длинную строку раскладываем только видимым окном символов: иначе
		// DWrite формирует и растеризует всю строку при каждой перерисовке
		// (а курсор мигает дважды в секунду) — окно «замирает» на длинных
		// строках, что и выглядело как зависание после вставки.
		std::size_t from_byte = row.from;
		std::size_t to_byte = std::min(row.to, bytes.size());
		std::wstring wide;
		if (ed.word_wrap() || bytes.size() <= kLineSliceBytes)
		{
			// При переносе ряд короткий (не длиннее предела раскладки).
			wide = utf8_to_wide(bytes.substr(from_byte, to_byte - from_byte));
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
			to_byte = std::min(std::max(b1, from_byte), bytes.size());
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

		// Цвет и подчёркивание пометки ([Colors] ColorMark) на самих буквах:
		// DWrite рисует текст цветом drawing-эффекта, поэтому нужные
		// диапазоны красим кистью пометки. Эффекты хранятся в раскладке,
		// а раскладка кэшируется — при смене пометок старые снимаем.
		{
			// Длина в единицах UTF-16 (DWrite считает позиции в них): байты
			// продолжения пропускаем, четырёхбайтовый символ — две единицы.
			const auto units = [](std::string_view v)
			{
				std::size_t n = 0;
				for (const char ch : v)
				{
					const auto u = static_cast<unsigned char>(ch);
					if ((u & 0xC0U) == 0x80U)
						continue;
					n += (u >= 0xF0U) ? 2U : 1U;
				}
				return static_cast<std::size_t>(n);
			};
			std::vector<DWRITE_TEXT_RANGE> want;
			if (mark_brush_ != nullptr)
			{
				// Пометки — смещения в буфере, ряд — смещения в строке.
				const std::size_t line_start = ed.buffer().line_start(i);
				const std::size_t abs_from = line_start + from_byte;
				const std::size_t abs_to = line_start + to_byte;
				for (const auto& m : yofy_marks)
				{
					const std::size_t a = std::max(m.first, abs_from);
					const std::size_t b = std::min(m.second, abs_to);
					if (a >= b)
						continue;
					const auto s0 = static_cast<UINT32>(units(bytes.substr(from_byte, a - abs_from)));
					const auto s1 = static_cast<UINT32>(units(bytes.substr(from_byte, b - abs_from)));
					if (s1 > s0)
						want.push_back(DWRITE_TEXT_RANGE{s0, s1 - s0});
				}
			}
			bool same = want.size() == slot->mark_ranges.size();
			for (std::size_t k = 0; same && k < want.size(); ++k)
				same = want[k].startPosition == slot->mark_ranges[k].startPosition &&
					want[k].length == slot->mark_ranges[k].length;
			if (!same)
			{
				for (const DWRITE_TEXT_RANGE& r : slot->mark_ranges)
				{
					slot->layout->SetDrawingEffect(nullptr, r);
					slot->layout->SetUnderline(FALSE, r);
				}
				for (const DWRITE_TEXT_RANGE& r : want)
				{
					slot->layout->SetDrawingEffect(mark_brush_, r);
					slot->layout->SetUnderline(TRUE, r);
				}
				slot->mark_ranges = std::move(want);
			}
		}
		// Срез рисуется со сдвигом на ширину отброшенного начала строки.
		const float slice_x = ed.row_width(row, from_byte);
		rt_->DrawTextLayout(D2D1::Point2F(text_x0() + slice_x - scroll_x, line_y), slot->layout, brush_,
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
		const float x = text_x0() + ed.caret_x() - scroll_x;
		const float y = text_y0 + ed.caret_y() - scroll_y;
		brush_->SetColor(theme_.caret);
		rt_->FillRectangle(D2D1::RectF(x, y + 1.0F, x + std::max(1.0F, dpi_scale_), y + lh - 1.0F), brush_);
	}

	rt_->PopAxisAlignedClip();

	// Панель вопросов поверх затемнённого документа: видно, что ввод ждут.
	if (ask != nullptr && dim_brush_ != nullptr)
		rt_->FillRectangle(D2D1::RectF(0.0F, text_y0, width_dip_, text_y0 + text_h), dim_brush_);

	// Слово, про которое спрашивает ёфикация, подсвечиваем в тексте — как в
	// AskPanel оригинала: видно, к чему относится вопрос. Подсветка живёт и
	// между вопросами (весь проход), поэтому не мигает.
	if (word_highlight.second > word_highlight.first)
		draw_word_highlight(ed, scroll_x, scroll_y, text_h, word_highlight.first,
							word_highlight.second);

	if (find != nullptr)
		draw_find_bar(*find);
	draw_status(status);
	if (ask != nullptr)
		draw_ask(*ask);
	if (bar != nullptr)
		draw_menu_bar(*bar);
	if (options != nullptr)
		draw_options(*options);
	if (dict != nullptr)
		draw_dict_editor(*dict);

	const HRESULT hr = rt_->EndDraw();
	if (hr == D2DERR_RECREATE_TARGET)
	{
		if (rt_ != nullptr)
		{
			rt_->Release();
			rt_ = nullptr;
		}
		// После потери устройства все кисти недействительны — освобождаем их
		// вместе с кэшем раскладок, ссылающимся на кисть пометки.
		for (ID2D1SolidColorBrush** b : {&brush_, &accent_brush_, &dim_brush_, &mark_brush_})
		{
			if (*b != nullptr)
			{
				(*b)->Release();
				*b = nullptr;
			}
		}
		for (CachedLine& c : cache_)
		{
			if (c.layout != nullptr)
				c.layout->Release();
		}
		cache_.clear();
	}
}

} // namespace yo::gui
