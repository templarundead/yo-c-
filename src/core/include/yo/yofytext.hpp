// Ёфикация документа: адаптер между TextBuffer (UTF-8) и движком
// yo::Yoficator (UTF-16, построчно, с доступом к соседним строкам).
// Не знает про Win32 — используется GUI и тестами.
#pragma once

#include "yo/textbuf.hpp"
#include "yo/yofy.hpp"

#include <cstddef>
#include <functional>
#include <string>
#include <utility>
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
	std::size_t reverted = 0;	// сколько замен снято «Откатом»
	bool cancelled = false;		// обработка прервана пользователем
	bool rolled_back = false;	// (устарело) проход отменён целиком
	bool changed = false;		// текст документа изменился
	// Абсолютные байтовые диапазоны именно тех символов, которые заменены
	// (е→ё и обратно), в документе после прохода. По ним GUI красит пометки:
	// подсвечивать все «ё» диапазона нельзя — подсвечивались бы и те, что
	// стояли в тексте изначально.
	std::vector<std::pair<std::size_t, std::size_t>> marks;
};

// Строка, к которой перешёл проход (для подсветки/прокрутки в GUI).
// Вызывается перед разбором каждой строки, до возможных вопросов движка.
using YofyLineFn = std::function<void(std::size_t line)>;

// Проверка «пора прекращать» — вызывается перед каждой строкой. GUI отдаёт
// сюда обработку нажатий: без этого долгий проход нельзя ни прервать, ни
// закрыть окно (окно выглядит зависшим).
using YofyCancelFn = std::function<bool()>;

// Ёфицировать строки, пересекающие [from, to), одной группой undo.
// Ответы на вопросы движка задаются через Yoficator::set_ask у вызывающего.
// «Откат» снимает последнюю сделанную замену прохода (один шаг), а не весь
// проход: остальные ответы пользователя сохраняются.
YofyResult yofify_range(TextBuffer& buf, std::size_t from, std::size_t to, Yoficator& yofy,
						const YofyLineFn& on_line = {}, const YofyCancelFn& cancel = {});

// Ёфицировать диапазон кусками по chunk_lines строк. Память ограничена
// куском (а не размером файла), прогресс и отмена — по строкам документа,
// а все правки попадают в одну группу undo: пользователю всё равно нужен
// один «Отменить» на весь проход. Возвращает суммарный результат.
//
// Ограничение: «Откат» в панели вопросов помнит замены только текущего
// куска (в оригинале — всего прохода); кусок в 2048 строк на практике
// покрывает все вопросы, которые успевают задать.
YofyResult yofify_document(TextBuffer& buf, std::size_t from, std::size_t to, Yoficator& yofy,
						   std::size_t chunk_lines = 2048, const YofyLineFn& on_line = {},
						   const YofyCancelFn& cancel = {});

} // namespace yo
