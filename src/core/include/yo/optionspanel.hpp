// Окно настроек: раскладка и пункты — один в один с OptionsForm оригинала
// (src/OptionsPage.dfm в репозитории vgiv/yo).
//
// Никакого Win32: панель описывается числами (клиентские DIP) и строками
// UTF-8, поэтому геометрия и попадание кликов проверяются тестами, а GUI
// только рисует прямоугольники. В отличие от оригинала (модальный VCL-диалог
// с белым фоном) здесь — наложение в главном окне в тёмной теме, а вместо
// вкладок TPageControl — строка-переключатель вкладок.
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace yo {

// Пункт настроек или кнопка окна. Порядок значений повторяет порядок
// контролов в OptionsPage.dfm; «наши» пункты (словари, без вопросов)
// собраны в отдельной вкладке, чтобы вкладки «Ёфикация» и «Редактор»
// совпадали с оригиналом строка в строку.
enum class OptionChoice : std::uint8_t
{
	// вкладки
	tab_yofy,	// «Ёфикация»
	tab_editor, // «Редактор»
	tab_dicts,	// «Словари» (наше дополнение)

	// вкладка «Ёфикация» — порядок как в OptionsPage.dfm
	check_yo,			// Проверять расстановку "ё"
	no_var_only,		// Только бесспорные замены
	var_only,			// Только интерактивные замены
	always_ask,			// Всегда подтверждать замену
	to_confirm_abbr,	// ... для сокращений
	to_confirm_cap,		// ... для имён собственных
	to_confirm_ellipsis, // ... для оборванных слов
	propose_last,		// Предлагать последний вариант замены
	regexs,				// Использование словаря регулярных выражений
	fb_format,			// Учитывать формат "FictionBook"
	lines_below,		// Количество строк промотки
	lines_below_minus,
	lines_below_plus,
	fast_scroll, // Быстрая промотка

	// вкладка «Редактор»
	mark,			 // Помечать исправленное
	mark_color,		 // Цвет
	back_mark_color, // Фон
	word_wrap,		 // Заворачивать текст ("Word wrap")
	recall_last_file,// Запоминать последний файл
	font_size,		 // Шрифт (у нас — размер текста)
	font_smaller,
	font_bigger,
	auto_unicode,	// Автораспознавать формат Unicode
	show_toolbar,	// Показывать панель инструментов
	to_confirm_close, // Не подтверждать закрытие программы

	// вкладка «Словари» (наше дополнение)
	dictionary,		  // основной словарь (клик — выбор файла)
	regex_dictionary, // словарь РВ
	silent_yofy,	  // ёфицировать без вопросов (наше)

	// кнопки окна — как в оригинале: bOK, bCancel, bOKSave
	apply,			// «Применить»
	cancel,			// «Отменить»
	apply_remember, // «Применить и запомнить»
};

// Текущие значения — то, что показывается в окне.
struct OptionsState
{
	int tab = 0; // 0 — «Ёфикация», 1 — «Редактор», 2 — «Словари»

	// [Yo] — ключи названы как в оригинале (Main.pas §ReadOptions)
	bool check_yo = true;		   // CheckYo
	bool no_var_only = false;	   // NoVarOnly
	bool var_only = false;		   // VarOnly
	bool always_ask = false;	   // AlwaysAsk
	bool to_confirm_abbr = true;   // ToConfirmAbbr
	bool to_confirm_cap = false;   // ToConfirmCap
	bool to_confirm_ellipsis = true; // ToConfirmEllipsis
	bool propose_last = true;	   // ProposeLast
	bool regexs = true;			   // RegExprs
	bool fb_format = false;		   // FBFormat
	int lines_below = 2;		   // LinesBelow
	bool fast_scroll = false;	   // FastScroll

	bool mark = true;		   // Mark
	int mark_color = 0;		   // [Colors] ColorMark (индекс палитры)
	int back_mark_color = 1;   // [Colors] ColorBackMark
	bool word_wrap = true;	   // WordWrap
	bool recall_last_file = true; // RecallLastFile
	float font_size = 15.0F;   // FontSize
	bool auto_unicode = true;  // AutoUnicode
	bool show_toolbar = false; // [ToolBar] ShowToolBar
	bool to_confirm_close = false; // ToConfirmClose

	// наши пункты
	bool silent_yofy = false;
	std::string dictionary;		  // как показывать: путь или имя по умолчанию
	std::string regex_dictionary;
};

// Цвет из палитры окна настроек: имя для строки и значение для темы.
struct OptionsColor
{
	std::uint32_t rgb = 0; // 0x00BBGGRR, как COLORREF в Win32
	std::string name;
};

// Палитра для «Цвет»/«Фон» помеченных замен (оригинал брал любой цвет из
// ColorDialog; у нас — фиксированный набор, чтобы раскладка была тестируемой).
[[nodiscard]] std::span<const OptionsColor> options_colors();
[[nodiscard]] int options_next_color(int current);
[[nodiscard]] const OptionsColor& options_color_at(int index);

struct OptionRowBox
{
	OptionChoice what = OptionChoice::always_ask;
	std::string label;
	std::string value; // «2» для чисел, имя цвета, путь для словарей
	bool check = false;	 // строка-галочка
	bool checked = false;
	bool path = false;	 // строка выбора файла
	bool tab = false;	 // заголовок вкладки
	bool tab_active = false;
	float x = 0.0F;
	float y = 0.0F;
	float w = 0.0F;
	float h = 0.0F;
};

struct OptionButtonBox
{
	OptionChoice what = OptionChoice::cancel;
	std::string label;
	float x = 0.0F;
	float y = 0.0F;
	float w = 0.0F;
	float h = 0.0F;
};

struct OptionsLayout
{
	bool visible = false;
	float x = 0.0F;
	float y = 0.0F;
	float w = 0.0F;
	float h = 0.0F;
	std::string title = "Опции"; // заголовок окна оригинала
	std::vector<OptionRowBox> rows; // заголовки вкладок идут первыми
	std::vector<OptionButtonBox> buttons;

	// Индекс строки/кнопки под точкой (-1 — мимо).
	[[nodiscard]] int row_at(float px, float py) const;
	[[nodiscard]] int button_at(float px, float py) const;
};

// Размер шрифта в окне: целое число (как FontSize в ini).
[[nodiscard]] std::string options_font_text(float size);

// Раскладка окна по центру клиентской области. `line_h` — высота строки
// интерфейсного шрифта (DIP): размеры окна не зависят от масштаба текста.
[[nodiscard]] OptionsLayout layout_options(const OptionsState& state, float width, float height,
										   float line_h);

} // namespace yo
