// Окно настроек: раскладка и пункты (аналог OptionsForm оригинала).
//
// Никакого Win32: панель описывается числами (клиентские DIP) и строками
// UTF-8, поэтому геометрия и попадание кликов проверяются тестами, а GUI
// только рисует прямоугольники. В отличие от оригинала (модальный VCL-диалог
// с белым фоном) здесь — наложение в главном окне в тёмной теме.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace yo {

// Пункт настроек или кнопка окна.
enum class OptionChoice : std::uint8_t
{
	always_ask,		  // спрашивать про все слова
	silent_yofy,	  // ёфицировать молча
	mark,			  // подсвечивать замены
	propose_last,	  // предлагать последний вариант
	regexs,			  // использовать словарь РВ
	to_confirm_abbr,  // спрашивать про сокращения
	dictionary,		  // путь к основному словарю (клик — выбор файла)
	regex_dictionary, // путь к словарю РВ
	font_smaller,
	font_bigger,
	apply,			  // применить и закрыть
	apply_remember,	  // применить и записать в yo.ini
	cancel,
};

// Текущие значения — то, что показывается в окне.
struct OptionsState
{
	bool always_ask = false;
	bool silent_yofy = false;
	bool mark = true;
	bool propose_last = true;
	bool regexs = true;
	bool to_confirm_abbr = true;
	float font_size = 15.0F;
	std::string dictionary;		  // как показывать: путь или имя файла по умолчанию
	std::string regex_dictionary;
};

struct OptionRowBox
{
	OptionChoice what = OptionChoice::always_ask;
	std::string label;
	std::string value; // «15» для размера, путь для словарей
	bool check = false;	   // строка-галочка
	bool checked = false;
	bool path = false;	   // строка выбора файла
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
	std::string title = "Настройки";
	std::vector<OptionRowBox> rows;
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
