// Панель вопросов ёфикации: тексты и раскладка.
//
// Никакого Win32: панель описывается числами (клиентские пиксели) и строками
// UTF-8, поэтому её геометрия проверяется тестами на любой платформе, а GUI
// только рисует прямоугольники и переводит нажатия в ответы движку.
#pragma once

#include "yo/yofy.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace yo {

// Тексты панели: варианты написания и причина вопроса.
struct AskTexts
{
	std::string variants; // «1  Е: все          2  Ё: всё»
	std::string reason;	  // «вероятно, здесь должна быть Ё»
};

[[nodiscard]] AskTexts make_ask_texts(const AskRequest& req);

// Что можно ответить (совпадает с AskAnswer, но без служебных значений).
enum class AskChoice : std::uint8_t
{
	ye,
	yo,
	ye_all,
	yo_all,
	skip,
	undo,
	cancel,
};

[[nodiscard]] AskAnswer to_answer(AskChoice choice) noexcept;

struct AskButtonBox
{
	AskChoice choice = AskChoice::cancel;
	std::string label; // «1  Е», «Esc  Прервать»
	float x = 0.0F;
	float y = 0.0F;
	float w = 0.0F;
	float h = 0.0F;
};

// Что за строка в панели: заголовок, кусок предложения или строка вариантов.
enum class AskRowKind : std::uint8_t
{
	title,
	context,
	variant,
};

struct AskLayout
{
	bool visible = false;
	// Панель во всю ширину у нижнего края (как AskPanel оригинала).
	bool bottom = false;
	float x = 0.0F; // панель, клиентские пиксели
	float y = 0.0F;
	float w = 0.0F;
	float h = 0.0F;

	struct Row
	{
		std::string text;
		float y = 0.0F;
		float h = 0.0F;
		AskRowKind kind = AskRowKind::context;
	};
	std::vector<Row> rows; // варианты, причина, контекст
	std::vector<AskButtonBox> buttons;

	// Индекс кнопки для ответа (-1, если такой кнопки нет).
	[[nodiscard]] int index_of(AskChoice choice) const noexcept;
};

struct AskLayoutInput
{
	float client_w = 0.0F;
	float client_h = 0.0F;
	float status_h = 0.0F;
	float padding = 0.0F;
	float line_height = 0.0F;
	float char_width = 0.0F;
	float max_chars = 96.0F; // предел ширины панели в символах
	std::string variants;	 // из make_ask_texts
	std::string reason;
	std::string context; // предложение с обрабатываемым словом (может быть пустым)
	// Режим «полоса во всю ширину снизу», как AskPanel оригинала: заголовок
	// по центру, кнопки в две колонки.
	bool bottom_bar = false;
	std::string word1; // вариант без ё — подписывает кнопку «1»
	std::string word2; // вариант с ё — подписывает кнопку «2»
};

// measure — ширина строки в пикселях шрифтом интерфейса. Если не задана,
// считается моноширинной по char_width.
using AskMeasureFn = std::function<float(std::string_view)>;

// Раскладка панели: строки текста (с подрезкой «…») и кнопки, уложенные
// в несколько рядов, если по ширине не влезают. Панель стоит над строкой
// состояния, по центру окна.
[[nodiscard]] AskLayout layout_ask_panel(const AskLayoutInput& in, const AskMeasureFn& measure);

} // namespace yo
