// Ёфикация документа: адаптер между TextBuffer (UTF-8) и движком
// yo::Yoficator (UTF-16, построчно, с доступом к соседним строкам).
// Не знает про Win32 — используется GUI и тестами.
#pragma once

#include "yo/textbuf.hpp"
#include "yo/yofy.hpp"

#include <cstddef>
#include <functional>
#include <string>
#include <unordered_map>

namespace yo {

// Строки документа в UTF-16 — движку нужен доступ к любой строке
// (контекст РВ, сокращения, многоточия). Строки декодируются по требованию
// и кэшируются: ёфикация идёт подряд, поэтому кэш попадает.
class BufferLineSource final : public LineSource
{
public:
	explicit BufferLineSource(const TextBuffer& buf) : buf_(buf) {}

	[[nodiscard]] std::size_t line_count() const override { return buf_.line_count(); }
	[[nodiscard]] const UString* line(std::size_t index) const override;

private:
	const TextBuffer& buf_;
	mutable std::unordered_map<std::size_t, UString> cache_;
};

struct YofyResult
{
	std::size_t replaced = 0;	// сколько символов е<->ё заменено
	std::size_t lines = 0;		// сколько строк обработано
	std::size_t asked = 0;		// сколько раз движок спрашивал
	bool cancelled = false;		// обработка прервана пользователем
	bool rolled_back = false;	// пользователь выбрал «Откат»: изменений нет
	bool changed = false;		// текст документа изменился
};

// Строка, к которой перешёл проход (для подсветки/прокрутки в GUI).
// Вызывается перед разбором каждой строки, до возможных вопросов движка.
using YofyLineFn = std::function<void(std::size_t line)>;

// Ёфицировать строки, пересекающие [from, to), одной группой undo.
// Ответы на вопросы движка задаются через Yoficator::set_ask у вызывающего.
// Если пользователь выбрал «Откат», документ не меняется вовсе.
YofyResult yofify_range(TextBuffer& buf, std::size_t from, std::size_t to, Yoficator& yofy,
						const YofyLineFn& on_line = {});

} // namespace yo
