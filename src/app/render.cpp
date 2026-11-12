#include "render.hpp"

#include "yo/ranges.hpp"
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
		brush_->SetColor(active ? theme_.border_hot : theme_.border);
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
		brush_->SetColor(lit ? theme_.control_hot : theme_.control);
		rt_->FillRectangle(b.rect, brush_);
		brush_->SetColor(lit ? theme_.border_hot : theme_.border);
		rt_->DrawRectangle(b.rect, brush_, 1.0F);
		brush_->SetColor(lit ? theme_.text : theme_.status_fg);
		rt_->DrawTextW(b.label.c_str(), static_cast<UINT32>(b.label.size()), fmt_center_, b.rect, brush_,
					   D2D1_DRAW_TEXT_OPTIONS_CLIP);
	}

}

// Подсветка найденных совпадений: жёлтая заливка под текстом.

void Renderer::draw_options(const OptionsView& view)
{
	if (rt_ == nullptr || fmt_ui_ == nullptr || fmt_center_ == nullptr || brush_ == nullptr)
		return;

	// Затемнение документа под окном: видно, что ввод ждут здесь.
	if (dim_brush_ != nullptr)
		rt_->FillRectangle(D2D1::RectF(0.0F, 0.0F, width_dip_, height_dip_), dim_brush_);

	// Окно — как системный диалог в тёмной теме: фон панели и тонкая серая
	// рамка. Никакого синего контура вокруг окна.
	brush_->SetColor(theme_.status_bg);
	rt_->FillRectangle(view.rect, brush_);
	brush_->SetColor(theme_.border);
	rt_->DrawRectangle(view.rect, brush_, 1.0F);

	// Заголовок «Опции» — компактно, в одну строку интерфейсного шрифта.
	const float title_top = view.rect.top + 6.0F;
	brush_->SetColor(theme_.text);
	rt_->DrawTextW(view.title.c_str(), static_cast<UINT32>(view.title.size()), fmt_ui_,
				   D2D1::RectF(view.rect.left + 10.0F, title_top, view.rect.right - 10.0F,
							   title_top + ui_line_h_),
				   brush_, D2D1_DRAW_TEXT_OPTIONS_CLIP);

	for (std::size_t i = 0; i < view.rows.size(); ++i)
	{
		const OptionsRowView& row = view.rows[i];
		const bool focused = static_cast<int>(i) == view.focus;
		if (row.tab)
		{
			// Вкладки — как ярлыки TPageControl: без заливки и рамки, активная
			// выделена подписью и линией снизу; фокус клавиатуры — тонкая рамка.
			if (focused)
			{
				brush_->SetColor(theme_.border);
				rt_->DrawRectangle(row.rect, brush_, 1.0F);
			}
			brush_->SetColor(row.tab_active ? theme_.text : theme_.status_fg);
			rt_->DrawTextW(row.label.c_str(), static_cast<UINT32>(row.label.size()), fmt_center_,
						   row.rect, brush_, D2D1_DRAW_TEXT_OPTIONS_CLIP);
			if (row.tab_active)
			{
				brush_->SetColor(theme_.border_hot);
				rt_->FillRectangle(D2D1::RectF(row.rect.left + 2.0F, row.rect.bottom - 2.0F,
											   row.rect.right - 2.0F, row.rect.bottom),
								   brush_);
			}
			continue;
		}
		// Строка в фокусе — «фокусная рамка» системного вида, без синей заливки.
		if (focused)
		{
			brush_->SetColor(theme_.border);
			rt_->DrawRectangle(row.rect, brush_, 1.0F);
		}
		float x = row.rect.left + 4.0F;
		if (row.check)
		{
			// Чекбокс: квадрат с галочкой (линиями), как у системного контроля.
			const float box = std::clamp(row.rect.bottom - row.rect.top - 4.0F, 11.0F, 13.0F);
			const float top = row.rect.top + (row.rect.bottom - row.rect.top - box) * 0.5F;
			const D2D1_RECT_F mark_rect = D2D1::RectF(x, top, x + box, top + box);
			brush_->SetColor(row.enabled ? theme_.status_fg : theme_.border);
			rt_->DrawRectangle(mark_rect, brush_, 1.0F);
			if (row.checked)
			{
				brush_->SetColor(row.enabled ? theme_.text : theme_.status_fg);
				const float w = box;
				const float h = box;
				rt_->DrawLine(D2D1::Point2F(mark_rect.left + w * 0.22F, mark_rect.top + h * 0.52F),
							  D2D1::Point2F(mark_rect.left + w * 0.42F, mark_rect.top + h * 0.74F),
							  brush_, 1.6F);
				rt_->DrawLine(D2D1::Point2F(mark_rect.left + w * 0.42F, mark_rect.top + h * 0.74F),
							  D2D1::Point2F(mark_rect.left + w * 0.78F, mark_rect.top + h * 0.26F),
							  brush_, 1.6F);
			}
			x += box + 7.0F;
		}
		// Недоступная строка (правила OptionsPage.cbClick) — приглушённая;
		// «синие» пути словарей больше не подсвечиваются.
		brush_->SetColor(row.enabled ? theme_.text : theme_.status_fg);
		// Значение — у правого края строки, но левее кнопок «−»/«+».
		const float value_right = row.rect.right - row.value_inset;
		const float value_w = row.value.empty() ? 0.0F : measure_ui(row.value) + 10.0F;
		rt_->DrawTextW(row.label.c_str(), static_cast<UINT32>(row.label.size()), fmt_ui_,
					   D2D1::RectF(x, row.rect.top, std::max(x + 10.0F, value_right - value_w),
								   row.rect.bottom),
					   brush_, D2D1_DRAW_TEXT_OPTIONS_CLIP);
		if (!row.value.empty())
		{
			brush_->SetColor(row.enabled ? theme_.status_fg : theme_.border);
			rt_->DrawTextW(row.value.c_str(), static_cast<UINT32>(row.value.size()), fmt_ui_,
						   D2D1::RectF(value_right - value_w, row.rect.top, value_right, row.rect.bottom),
						   brush_, D2D1_DRAW_TEXT_OPTIONS_CLIP);
		}
	}

	for (std::size_t i = 0; i < view.buttons.size(); ++i)
	{
		const OptionsButtonView& b = view.buttons[i];
		// Кнопки — нативные: заметный фон, серая рамка; под мышью/в фокусе
		// чуть светлее, но без синей заливки.
		const bool lit = static_cast<int>(i) == view.hot || b.primary;
		brush_->SetColor(lit ? theme_.control_hot : theme_.control);
		rt_->FillRectangle(b.rect, brush_);
		brush_->SetColor(lit ? theme_.border_hot : theme_.border);
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


// Редактор словарей: таблица, поля ввода, кнопки и строка сообщения.
void Renderer::draw_dict_editor(const DictView& view)
{
	if (rt_ == nullptr || fmt_ui_ == nullptr || fmt_center_ == nullptr || brush_ == nullptr)
		return;

	if (dim_brush_ != nullptr)
		rt_->FillRectangle(D2D1::RectF(0.0F, 0.0F, width_dip_, height_dip_), dim_brush_);

	brush_->SetColor(theme_.status_bg);
	rt_->FillRectangle(view.rect, brush_);
	brush_->SetColor(theme_.border);
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
		brush_->SetColor(focus ? theme_.border_hot : theme_.border);
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
			// Текущая строка — системная подсветка, без синей заливки.
			brush_->SetColor(theme_.control);
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
		D2D1_COLOR_F fill = theme_.control;
		D2D1_COLOR_F border = theme_.border;
		if (b.toggle && b.checked)
		{
			fill = theme_.control_hot;
			border = theme_.border_hot;
		}
		if (b.hot || b.primary)
		{
			fill = theme_.control_hot;
			border = theme_.border_hot;
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
			brush_->SetColor(theme_.control);
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
	brush_->SetColor(theme_.border);
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
			// Системная подсветка пункта: серый фон, без синего.
			brush_->SetColor(theme_.control);
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
			brush_->SetColor(theme_.text);
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
	// Разделитель сверху — системный тон, без синей полосы.
	brush_->SetColor(theme_.border);
	rt_->FillRectangle(D2D1::RectF(view.panel.left, view.panel.top, view.panel.right,
								   view.panel.top + 1.0F),
					   brush_);

	for (const AskRow& row : view.rows)
	{
		if (row.text.empty())
			continue;
		// Заголовок панели — обычным цветом текста (без синей подписи).
		const D2D1_COLOR_F base = row.kind == AskKind::dim ? theme_.status_fg : theme_.text;
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
		// Нативный вид: у «Е»/«Ё» нет заливки — только цветная подпись
		// (синяя/зелёная), у остальных кнопок фон заметно отличается от фона
		// панели, иначе кнопки «сливались» с полосой. Рамка появляется только
		// в фокусе/под мышью, без синих заливок.
		D2D1_COLOR_F text = theme_.text;
		D2D1_COLOR_F fill = lit ? theme_.control_hot : theme_.control;
		D2D1_COLOR_F border = lit ? theme_.border_hot : theme_.border;
		bool has_fill = true;
		switch (b.tone)
		{
		case AskTone::ye:
			text = theme_.ye_text;
			has_fill = false;
			break;
		case AskTone::yo:
			text = theme_.yo_text;
			has_fill = false;
			break;
		case AskTone::neutral:
		default:
			break;
		}
		if (has_fill)
		{
			brush_->SetColor(fill);
			rt_->FillRectangle(b.rect, brush_);
			brush_->SetColor(border);
			rt_->DrawRectangle(b.rect, brush_, 1.0F);
		}
		else if (lit)
		{
			// Только рамка: «1»/«2» остаются цветными подписями на фоне полосы.
			brush_->SetColor(b.tone == AskTone::ye ? theme_.ye_text : theme_.yo_text);
			rt_->DrawRectangle(b.rect, brush_, 1.0F);
			brush_->SetColor(text);
		}
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
	// Прямоугольники слова под вопросом: считаются по раскладке в цикле
	// отрисовки, а рисуются поверх затемнения панели вопросов.
	std::vector<D2D1_RECT_F> word_rects;

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
	// Первый проход — раскладки и геометрия рядов; второй — фон пометок и
	// подсветки (под буквами); третий — сами буквы. Подсветка слова считается
	// по раскладке, поэтому совпадает с нарисованными буквами и при переносе.
	struct RowDraw
	{
		const Editor::Row* row = nullptr;
		// Индекс в cache_, а не указатель: push_back ниже перевыделяет вектор,
		// и указатель на элемент становился висячим (падение при вставке
		// большого текста, когда раскладок становится много).
		std::size_t slot = 0;
		std::size_t line = 0;
		std::size_t line_start = 0;
		std::size_t from = 0; // срез в байтах строки
		std::size_t to = 0;
		std::string_view bytes;
		float y = 0.0F;
		float slice_x = 0.0F;
	};
	std::vector<RowDraw> draws;
	draws.reserve(rows.size());
	for (CachedLine& c : cache_)
		c.used = false;
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

		RowDraw d;
		d.row = &row;
		d.slot = static_cast<std::size_t>(slot - cache_.data());
		d.line = i;
		d.line_start = ed.buffer().line_start(i);
		d.from = from_byte;
		d.to = to_byte;
		d.bytes = bytes;
		d.y = line_y;
		// Срез рисуется со сдвигом на ширину отброшенного начала строки.
		d.slice_x = ed.row_width(row, from_byte);
		draws.push_back(d);
	}

	// Длина префикса среза в единицах UTF-16 (DWrite считает позиции в них):
	// байты продолжения пропускаем, четырёхбайтовый символ — две единицы.
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
	// x позиции внутри раскладки (в её координатах).
	const auto hit_x = [](IDWriteTextLayout* layout, UINT32 pos) -> float
	{
		FLOAT x = 0.0F;
		FLOAT y = 0.0F;
		DWRITE_HIT_TEST_METRICS m{};
		if (layout == nullptr || FAILED(layout->HitTestTextPosition(pos, FALSE, &x, &y, &m)))
			return 0.0F;
		return x;
	};
	// Абсолютный байтовый диапазон → отрезок на экране по самой раскладке.
	const auto span = [&](const RowDraw& d, std::size_t a, std::size_t b, float& x1, float& x2) -> bool
	{
		if (d.slot >= cache_.size())
			return false;
		const CachedLine& slot = cache_[d.slot];
		if (slot.layout == nullptr || d.to > d.bytes.size())
			return false;
		const std::size_t off0 = a - d.line_start;
		const std::size_t off1 = b - d.line_start;
		if (off0 < d.from || off1 > d.to || off0 >= off1)
			return false;
		const UINT32 p0 = static_cast<UINT32>(units(d.bytes.substr(d.from, off0 - d.from)));
		const UINT32 p1 = static_cast<UINT32>(units(d.bytes.substr(d.from, off1 - d.from)));
		if (p1 <= p0)
			return false;
		const float base = text_x0() + d.slice_x - scroll_x;
		x1 = base + hit_x(slot.layout, p0);
		x2 = base + hit_x(slot.layout, p1);
		return x2 > x1;
	};
	// Фон диапазонов (пометки замен, найденное) — под буквами.
	const auto fill_bands = [&](const std::vector<std::pair<std::size_t, std::size_t>>& bands,
								const D2D1_COLOR_F& color)
	{
		if (bands.empty())
			return;
		brush_->SetColor(color);
		for (const RowDraw& d : draws)
		{
			// Двоичный поиск в ядре (yo::overlapping_ranges): перебор всех
			// пометок на каждый ряд «замирал» на больших файлах.
			const auto [first, last] =
				overlapping_ranges(bands, d.line_start + d.from, d.line_start + d.to);
			for (std::size_t k = first; k < last; ++k)
			{
				const std::size_t a = std::max(bands[k].first, d.line_start + d.from);
				const std::size_t b = std::min(bands[k].second, d.line_start + d.to);
				float x1 = 0.0F;
				float x2 = 0.0F;
				if (!span(d, a, b, x1, x2))
					continue;
				rt_->FillRectangle(D2D1::RectF(x1, d.y + 1.0F, x2, d.y + lh - 1.0F), brush_);
			}
		}
	};

	// Прямоугольники слова под вопросом — по раскладке ряда, поэтому рамка
	// стоит ровно по буквам (даже при переносе слов и крупном кегле).
	word_rects.clear();
	if (word_highlight.second > word_highlight.first)
	{
		for (const RowDraw& d : draws)
		{
			const std::size_t a = std::max(word_highlight.first, d.line_start + d.from);
			const std::size_t b = std::min(word_highlight.second, d.line_start + d.to);
			float x1 = 0.0F;
			float x2 = 0.0F;
			if (!span(d, a, b, x1, x2))
				continue;
			word_rects.push_back(D2D1::RectF(x1, d.y + 1.0F, x2, d.y + lh - 1.0F));
		}
	}

	// Фон пометки ([Colors] ColorBackMark), найденного и слова под вопросом —
	// под буквами: заливка не «съедает» сами буквы. Текст пометки красится
	// ниже, drawing-эффектом раскладки; рамка слова рисуется поверх
	// затемнения панели вопросов.
	fill_bands(yofy_marks, yofy_back_color_);
	fill_bands(marks, D2D1::ColorF(0.95F, 0.80F, 0.20F, 0.35F));
	if (!word_rects.empty())
	{
		brush_->SetColor(D2D1::ColorF(0.72F, 0.16F, 0.16F, 0.30F));
		for (const D2D1_RECT_F& rect : word_rects)
			rt_->FillRectangle(rect, brush_);
	}

	// Сами буквы: цвет и подчёркивание пометки ([Colors] ColorMark) —
	// drawing effect и SetUnderline на диапазон раскладки. Цвет кисти
	// обновляем каждый раз: новый цвет из окна настроек должен быть виден
	// сразу, а не после пересоздания устройства (перезапуска программы).
	if (mark_brush_ != nullptr)
		mark_brush_->SetColor(yofy_text_color_);
	brush_->SetColor(theme_.text);
	for (RowDraw& d : draws)
	{
		std::vector<DWRITE_TEXT_RANGE> want;
		const std::size_t abs_from = d.line_start + d.from;
		const std::size_t abs_to = d.line_start + d.to;
		if (mark_brush_ != nullptr && !yofy_marks.empty())
		{
			const auto [first, last] = overlapping_ranges(yofy_marks, abs_from, abs_to);
			for (std::size_t k = first; k < last; ++k)
			{
				const std::size_t a = std::max(yofy_marks[k].first, abs_from);
				const std::size_t b = std::min(yofy_marks[k].second, abs_to);
				if (a >= b)
					continue;
				const auto s0 = static_cast<UINT32>(units(d.bytes.substr(d.from, a - abs_from)));
				const auto s1 = static_cast<UINT32>(units(d.bytes.substr(d.from, b - abs_from)));
				if (s1 > s0)
					want.push_back(DWRITE_TEXT_RANGE{ s0, s1 - s0 });
			}
		}
		if (d.slot >= cache_.size())
			continue;
		CachedLine& slot = cache_[d.slot];
		bool same = want.size() == slot.mark_ranges.size();
		for (std::size_t k = 0; same && k < want.size(); ++k)
			same = want[k].startPosition == slot.mark_ranges[k].startPosition &&
				   want[k].length == slot.mark_ranges[k].length;
		if (!same)
		{
			for (const DWRITE_TEXT_RANGE& r : slot.mark_ranges)
			{
				slot.layout->SetDrawingEffect(nullptr, r);
				slot.layout->SetUnderline(FALSE, r);
			}
			for (const DWRITE_TEXT_RANGE& r : want)
			{
				slot.layout->SetDrawingEffect(mark_brush_, r);
				slot.layout->SetUnderline(TRUE, r);
			}
			slot.mark_ranges = std::move(want);
		}
		rt_->DrawTextLayout(D2D1::Point2F(text_x0() + d.slice_x - scroll_x, d.y), slot.layout, brush_,
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
	// AskPanel оригинала: видно, к чему относится вопрос. Прямоугольники
	// посчитаны по раскладке ряда, поэтому рамка стоит ровно по буквам.
	brush_->SetColor(D2D1::ColorF(0.95F, 0.38F, 0.30F, 0.95F));
	for (const D2D1_RECT_F& rect : word_rects)
		rt_->DrawRectangle(rect, brush_, 1.0F);

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
