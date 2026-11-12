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

// Цвет: значение в формате COLORREF (0x00BBGGRR), как TColor в оригинале.
inline constexpr std::uint32_t kColorBlack = 0x00000000U;
inline constexpr std::uint32_t kColorRed = 0x000000FFU;
inline constexpr std::uint32_t kColorGreen = 0x00008000U;
inline constexpr std::uint32_t kColorBlue = 0x00FF0000U;
inline constexpr std::uint32_t kColorYellow = 0x0000FFFFU;
inline constexpr std::uint32_t kColorAqua = 0x00FFFF00U;
inline constexpr std::uint32_t kColorGray = 0x00808080U;
inline constexpr std::uint32_t kColorWhite = 0x00FFFFFFU;

// Стандартные цвета (пункт меню ColorDialog и подписи): имя + значение.
struct OptionsColor
{
	std::uint32_t rgb = 0; // 0x00BBGGRR, как COLORREF в Win32
	std::string name;
};

// Палитра окна настроек: имена для строк «Цвет»/«Фон» и быстрый выбор.
[[nodiscard]] std::span<const OptionsColor> options_colors();
[[nodiscard]] const OptionsColor& options_color_at(int index);
// Подпись цвета: имя из палитры («синий») или «#RRGGBB» для любого другого
// цвета, выбранного в ColorDialog.
[[nodiscard]] std::string options_color_text(std::uint32_t rgb);
// Значение из ini: COLORREF, а для совместимости — индекс палитры 0..5,
// который писали предыдущие сборки yo-c-.
[[nodiscard]] std::uint32_t options_color_from_ini(long long value);

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
	// [Colors] ColorMark / ColorBackMark — COLORREF (0x00BBGGRR), как в
	// оригинале; по умолчанию clBlue и clYellow.
	std::uint32_t mark_color = kColorBlue;
	std::uint32_t back_mark_color = kColorYellow;
	bool word_wrap = true;	   // WordWrap
	bool recall_last_file = true; // RecallLastFile
	float font_size = 15.0F;   // FontSize
	std::string font_name;	   // FontName (пусто — встроенный набор)
	bool auto_unicode = true;  // AutoUnicode
	bool show_toolbar = false; // [ToolBar] ShowToolBar
	bool to_confirm_close = false; // ToConfirmClose

	// наши пункты
	bool silent_yofy = false;
	std::string dictionary;		  // как показывать: путь или имя по умолчанию
	std::string regex_dictionary;
};

struct OptionRowBox
{
	OptionChoice what = OptionChoice::always_ask;
	std::string label;
	std::string value; // «2» для чисел, имя цвета, путь для словарей
	bool check = false;	 // строка-галочка
	bool checked = false;
	bool enabled = true; // доступность (правила блокировок, кнопки цветов)
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
// Подпись строки «Шрифт»: «Cascadia Mono, 11» или просто «15», если имя
// не выбрано (встроенный моноширинный набор).
[[nodiscard]] std::string options_font_label(const OptionsState& state);

// Правила взаимных блокировок окна «Опции»: повторяют OptionsPage.cbClick
// (AlwaysAsk выключает три ToConfirm*, VarOnly/NoVarOnly несовместимы,
// кнопки цветов доступны только при Mark) и добавляют взаимное исключение
// VarOnly ↔ AlwaysAsk: «только интерактивные замены» и «всегда спрашивать»
// одновременно смысла не имеют.
[[nodiscard]] bool option_enabled(const OptionsState& state, OptionChoice what);
// Переключение галочки с учётом правил (включает одно — снимает другое).
void options_toggle(OptionsState& state, OptionChoice what);
// Приведение загруженных из ini значений к согласованному виду
// (если в файле остались оба флага — побеждает AlwaysAsk).
void options_normalize(OptionsState& state);

// Раскладка окна по центру клиентской области. `line_h` — высота строки
// интерфейсного шрифта (DIP): размеры окна не зависят от масштаба текста.
[[nodiscard]] OptionsLayout layout_options(const OptionsState& state, float width, float height,
										   float line_h);

} // namespace yo
