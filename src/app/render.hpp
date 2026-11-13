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
#include <utility>
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
	// Участок строки с обрабатываемым словом — рисуем его красным, как
	// в AskPanel оригинала (видно, о каком слове вопрос).
	bool has_hot = false;
	std::size_t hot_from = 0;
	std::size_t hot_to = 0;
};

// Полоса поиска и замены: поля ввода и кнопки (геометрию считает App,
// чтобы клики и рисование совпадали).
struct FindBarButtonView
{
	D2D1_RECT_F rect{};
	std::wstring label;
	int id = 0;
	bool hot = false;
	bool checked = false; // «Aa» и «Слово» — включённые режимы
	bool toggle = false;
};

struct FindBarView
{
	D2D1_RECT_F rect{};
	D2D1_RECT_F query_rect{};
	D2D1_RECT_F replace_rect{};
	std::wstring query;
	std::wstring replace;
	int focus = 0;		// 0 — образец, 1 — замена
	bool replace_mode = false;
	float h = 0.0F;
	std::vector<FindBarButtonView> buttons;
};

// Окно настроек: строки (галочки, ссылки на файлы словарей, размер текста)
// и кнопки. Геометрию считает ядро (yo::layout_options), здесь — только вид.
struct OptionsRowView
{
	D2D1_RECT_F rect{};
	std::wstring label;
	std::wstring value;
	bool check = false;
	bool checked = false;
	bool path = false;
	bool tab = false;		 // заголовок вкладки («Ёфикация»/«Редактор»/«Словари»)
	bool tab_active = false; // активная вкладка — подчёркнутая
	bool enabled = true;	 // доступна (правила блокировок OptionsPage)
	float value_inset = 0.0F; // отступ значения справа (кнопки «−»/«+»)
};

struct OptionsButtonView
{
	D2D1_RECT_F rect{};
	std::wstring label;
	int choice = 0;
	bool primary = false;
};

struct OptionsView
{
	D2D1_RECT_F rect{};
	std::wstring title;
	std::vector<OptionsRowView> rows;
	std::vector<OptionsButtonView> buttons;
	int focus = -1;	 // строка под курсором/выбранная клавиатурой
	int hot = -1;	 // кнопка под мышью
};

// Редактор словарей: таблица слов/правил, поля фильтра и правки, кнопки.
// Геометрию считает App (ей же нужны прямоугольники для попадания мышью),
// содержимое и проверки — ядро (yo::DictEditorState).
struct DictRowView
{
	D2D1_RECT_F rect{};
	std::wstring text;
	bool current = false; // выбранная строка
	bool error = false;	  // у правила ошибка компиляции
};

struct DictButtonView
{
	D2D1_RECT_F rect{};
	std::wstring label;
	int id = 0;
	bool hot = false;
	bool primary = false;	// «Сохранить»
	bool checked = false;	// «?» включён
	bool toggle = false;	// кнопка-переключатель
};

struct DictView
{
	D2D1_RECT_F rect{};
	std::wstring title;
	std::wstring subtitle; // «словарь: N слов» / «правила РВ: M правил»
	D2D1_RECT_F filter_rect{};
	std::wstring filter;
	bool filter_focus = false;
	D2D1_RECT_F edit_rect{};
	std::wstring edit;
	bool edit_focus = false;
	std::wstring edit_label;
	std::vector<D2D1_RECT_F> row_rects; // фон списка — по числу видимых строк
	std::vector<DictRowView> rows;
	std::vector<DictButtonView> buttons;
	D2D1_RECT_F message_rect{};
	std::wstring message;
	bool message_error = false;
	std::wstring hint;
};

// Тон кнопки панели: «Е» — синяя, «Ё» — зелёная, остальные — системные.
enum class AskTone : std::uint8_t
{
	neutral,
	ye,
	yo,
};

struct AskButtonView
{
	D2D1_RECT_F rect{};
	std::wstring label;
	bool focus = false;
	bool hot = false;  // наведена мышь — видно, что кнопка нажимается
	AskTone tone = AskTone::neutral;
};

struct AskView
{
	D2D1_RECT_F panel{};
	std::vector<AskRow> rows;
	std::vector<AskButtonView> buttons;
	// Обрабатываемое слово в документе (смещения в байтах): подсвечиваем его,
	// как это делает AskPanel оригинала, чтобы было видно, о чём вопрос.
	bool has_word = false;
	std::size_t word_from = 0;
	std::size_t word_to = 0;
	// Панель-полоса снизу отнимает у текста эту высоту (DIP): строка
	// с вопросом должна оставаться видимой над ней.
	float bottom_inset = 0.0F;
};

// Верхняя строка меню рисуется нами: системное меню на тёмной теме остаётся
// белым (в Windows нет API для его перекраски).
struct MenuItemView
{
	D2D1_RECT_F rect{};
	std::wstring label;
	bool separator = false;
	bool checked = false;
	bool enabled = true;
	bool hot = false;
};

struct MenuBarView
{
	std::vector<std::wstring> titles;
	std::vector<D2D1_RECT_F> title_rects;
	int active = -1;	  // индекс открытого меню (-1 — закрыто)
	int hot_title = -1;	  // подсвеченный заголовок (мышь/клавиатура)
	bool dropdown = false;
	D2D1_RECT_F panel{};
	std::vector<MenuItemView> items;
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
	// Нативные системные тона: кнопка отличается от фона панели настолько,
	// чтобы её было видно, но без «синих» заливок и рамок.
	D2D1_COLOR_F control = D2D1::ColorF(0.21F, 0.22F, 0.25F); // фон кнопки
	D2D1_COLOR_F control_hot = D2D1::ColorF(0.26F, 0.27F, 0.31F); // под мышью
	D2D1_COLOR_F border = D2D1::ColorF(0.34F, 0.36F, 0.40F);	// обычная рамка
	D2D1_COLOR_F border_hot = D2D1::ColorF(0.55F, 0.58F, 0.63F); // рамка в фокусе
	// «Е»/«Ё» в панели вопросов: только цвет подписи, без фона.
	D2D1_COLOR_F ye_text = D2D1::ColorF(0.55F, 0.73F, 1.00F);
	D2D1_COLOR_F yo_text = D2D1::ColorF(0.55F, 0.88F, 0.62F);
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
	// Шрифт текста: семейство (пусто — встроенный моноширинный набор) и кегль
	// в DIP. Выбор — диалогом «Шрифт» окна настроек (как TFontDialog).
	[[nodiscard]] const std::wstring& font_family() const noexcept { return font_family_; }
	bool set_editor_font(std::wstring_view family, float size_dip, HWND hwnd);
	[[nodiscard]] float char_width() const noexcept { return char_w_; }
	[[nodiscard]] float padding() const noexcept { return padding_; }
	[[nodiscard]] float line_height() const noexcept { return line_h_; }

	// Метрики шрифта интерфейса. Меню, панель вопросов и статус рисуются
	// им и НЕ масштабируются вместе с текстом (Ctrl+± меняет только текст).
	[[nodiscard]] float ui_line_height() const noexcept { return ui_line_h_; }
	[[nodiscard]] float ui_char_width() const noexcept { return ui_char_w_; }

	// Метрики текста панели вопросов: шрифт панели не меньше, чем в окне
	// документа по умолчанию, и не зависит от Ctrl+±.
	[[nodiscard]] float ask_line_height() const noexcept { return ask_line_h_; }
	[[nodiscard]] float ask_char_width() const noexcept { return ask_char_w_; }

	// Высота нижней полосы (статус + строка поиска) в пикселях.
	[[nodiscard]] float status_height() const noexcept;
	[[nodiscard]] float find_height() const noexcept;
	[[nodiscard]] float width_dip() const noexcept { return width_dip_; }
	[[nodiscard]] float height_dip() const noexcept { return height_dip_; }

	// Поле номера строки слева от текста (0 — выключено) и левый край текста.
	void set_gutter_width(float w) noexcept { gutter_w_ = std::max(0.0F, w); }
	[[nodiscard]] float gutter_width() const noexcept { return gutter_w_; }
	[[nodiscard]] float text_x0() const noexcept { return padding_ + gutter_w_; }

	// Ширина строки шрифтом интерфейса (Segoe UI) в пикселях — для кнопок.
	[[nodiscard]] float measure_ui(const std::wstring& text);
	// То же шрифтом панели вопросов (он крупнее интерфейсного).
	[[nodiscard]] float measure_ask(const std::wstring& text);

	// Полная перерисовка. `caret` — рисовать ли курсор (мигание),
	// `ask` — панель вопросов ёфикации поверх текста, `find` — полоса
	// поиска/замены, `marks` — подсвеченные совпадения (смещения в тексте).
	void paint(Editor& ed, const std::wstring& status, const FindBarView* find, bool caret,
			   const AskView* ask = nullptr, const MenuBarView* bar = nullptr,
			   const std::vector<std::pair<std::size_t, std::size_t>>& marks = {},
			   const std::vector<std::pair<std::size_t, std::size_t>>& yofy_marks = {},
			   const OptionsView* options = nullptr, const DictView* dict = nullptr,
			   const std::pair<std::size_t, std::size_t>& word_highlight = {});

	[[nodiscard]] float text_area_width() const noexcept;
	[[nodiscard]] float text_area_height() const noexcept;

	// Всё рисование идёт в DIP (независимых от DPI пикселях), а сообщения
	// мыши приходят в физических пикселях — App переводит их этим множителем.
	// Цвета помеченных замен — [Colors] ColorMark / ColorBackMark оригинала
	// (в оригинале это цвет текста и фон помеченных символов в RichEdit).
	void set_yofy_colors(D2D1_COLOR_F text_color, D2D1_COLOR_F back_color)
	{
		yofy_text_color_ = text_color;
		yofy_back_color_ = back_color;
	}
	[[nodiscard]] D2D1_COLOR_F yofy_text_color() const noexcept { return yofy_text_color_; }
	[[nodiscard]] D2D1_COLOR_F yofy_back_color() const noexcept { return yofy_back_color_; }

	[[nodiscard]] float dpi_scale() const noexcept { return dpi_scale_; }
	// Высота нашей строки меню в DIP: она внутри клиентской области.
	[[nodiscard]] float bar_height() const noexcept { return bar_h_; }

private:
	bool ensure_resources(HWND hwnd);
	bool measure_font(HWND hwnd);
	bool create_render_target(HWND hwnd);
	void draw_status(const std::wstring& status);
	void draw_find_bar(const FindBarView& view);
	void draw_options(const OptionsView& view);
	void draw_dict_editor(const DictView& view);
	// height_fraction = 1 — заливка ряда (фон пометки), меньше — тонкая полоса
	// у нижнего края (цвет пометки).
	void draw_ask(const AskView& view);
	void draw_menu_bar(const MenuBarView& bar);

	HWND hwnd_ = nullptr;
	ID2D1Factory* factory_ = nullptr;
	ID2D1HwndRenderTarget* rt_ = nullptr;
	IDWriteFactory* dwrite_ = nullptr;
	IDWriteTextFormat* fmt_ = nullptr;		// основной текст
	IDWriteTextFormat* fmt_ui_ = nullptr;	// статус/поиск/меню
	IDWriteTextFormat* fmt_center_ = nullptr; // панель вопросов: по центру
	IDWriteTextFormat* fmt_right_ = nullptr;  // номера строк: по правому краю
	IDWriteTextFormat* fmt_ask_ = nullptr;	  // панель вопросов: по левому краю
	ID2D1SolidColorBrush* brush_ = nullptr;
	ID2D1SolidColorBrush* accent_brush_ = nullptr; // подсветка ё
	// Цвет текста помеченных замен ([Colors] ColorMark): выдаётся раскладке
	// как drawing effect для нужных диапазонов.
	ID2D1SolidColorBrush* mark_brush_ = nullptr;
	ID2D1SolidColorBrush* dim_brush_ = nullptr;	   // затемнение под панелью вопросов

	float dpi_scale_ = 1.0F;
	float font_size_ = 15.0F;
	std::wstring font_family_; // пусто — Cascadia Mono/Consolas/Courier New
	// Размер шрифта панели вопросов: берётся из стартового размера текста и
	// дальше не меняется (Ctrl+± не должен двигать панель).
	float ask_font_size_ = 15.0F;
	float char_w_ = 8.0F;
	float line_h_ = 18.0F;
	float status_h_ = 20.0F;
	float find_h_ = 26.0F;
	float gutter_w_ = 0.0F;
	float padding_ = 6.0F;
	float bar_h_ = 26.0F;
	// Подсветка замен по умолчанию (совпадает с прежним зелёным).
	D2D1_COLOR_F yofy_text_color_ = D2D1::ColorF(0.25F, 0.75F, 0.35F, 1.0F);
	D2D1_COLOR_F yofy_back_color_ = D2D1::ColorF(0.25F, 0.75F, 0.35F, 0.30F);
	float ui_line_h_ = 16.0F;  // высота строки шрифта интерфейса
	float ui_char_w_ = 6.0F;
	float ask_line_h_ = 18.0F; // строка панели вопросов
	float ask_char_w_ = 7.0F;
	UINT width_px_ = 0;
	UINT height_px_ = 0;
	float width_dip_ = 0.0F;  // клиентская область в DIP (см. dpi_scale)
	float height_dip_ = 0.0F;

	struct CachedLine
	{
		std::size_t line = 0;
		std::size_t from = 0; // смещение среза в байтах (длинные строки)
		std::wstring text;
		IDWriteTextLayout* layout = nullptr;
		bool used = false;
		// Диапазоны, к которым уже применён цвет/подчёркивание пометки:
		// эффекты живут в раскладке, поэтому их надо снимать при изменении.
		std::vector<DWRITE_TEXT_RANGE> mark_ranges;
	};
	std::vector<CachedLine> cache_;

	Theme theme_;
};

} // namespace yo::gui
