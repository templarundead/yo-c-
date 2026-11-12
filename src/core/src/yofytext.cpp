#include "yo/yofytext.hpp"

#include "yo/codec.hpp"
#include "yo/utf.hpp"

#include <string>
#include <vector>

namespace yo {

namespace {
// Сколько байт UTF-8 занимают первые `codepoints` символов строки.
std::size_t utf8_prefix_bytes(std::string_view line, std::size_t codepoints)
{
	std::size_t bytes = 0;
	std::size_t count = 0;
	while (bytes < line.size() && count < codepoints)
	{
		const std::size_t len = utf8_char_len(line[bytes]);
		bytes += len == 0 ? 1 : len;
		++count;
	}
	return std::min(bytes, line.size());
}
} // namespace

const UString* BufferLineSource::line(std::size_t index) const
{
	if (index >= buf_.line_count())
		return nullptr;
	if (const auto it = cache_.find(index); it != cache_.end())
		return &it->second;
	const std::string_view bytes = buf_.line_text(index);
	UString text = decode_line({ reinterpret_cast<const std::uint8_t*>(bytes.data()), bytes.size() },
							   Encoding::utf8);
	const auto [it, ok] = cache_.emplace(index, std::move(text));
	(void)ok;
	return &it->second;
}

YofyResult yofify_range(TextBuffer& buf, std::size_t from, std::size_t to, Yoficator& yofy,
						const YofyLineFn& on_line, const YofyCancelFn& cancel)
{
	YofyResult result;
	if (buf.size() == 0 || from >= to)
		return result;

	const std::size_t first = buf.line_of(from);
	std::size_t last = buf.line_of(to - 1);
	if (last < first)
		last = first;

	BufferLineSource source(buf);
	std::vector<std::string> new_lines;
	new_lines.reserve(last - first + 1);
	// Тексты строк диапазона (только для строк с заменами): «Откат» может
	// снять замену в уже пройденной строке, и её придётся перекодировать.
	std::vector<UString> line_texts;
	line_texts.reserve(last - first + 1);

	// Сделанные замены в порядке применения: «Откат» снимает последнюю.
	struct Edit
	{
		std::size_t line = 0;
		std::size_t offset = 0;
		char32_t old_ch = 0;
		char32_t new_ch = 0;
	};
	std::vector<Edit> edits;

	// Текущая строка и её индекс: «Откат» снимает замену прямо в момент
	// ответа, поэтому стек замен и строка должны быть доступны из хуков.
	UString current;
	std::size_t current_index = 0;
	auto revert_last = [&]()
	{
		if (edits.empty())
			return; // отменять нечего — как «пропустить»
		const Edit e = edits.back();
		edits.pop_back();
		const std::size_t k = e.line - first;
		UString& target = e.line == current_index ? current : line_texts[k];
		if (e.offset < target.size() && target[e.offset] == e.new_ch)
			target[e.offset] = e.old_ch;
		if (e.line != current_index && k < new_lines.size())
			new_lines[k] = encode_line(target, Encoding::utf8);
		if (result.replaced > 0)
			--result.replaced;
		++result.reverted;
	};
	yofy.set_change_hook([&](const Change& c)
						 {
							 edits.push_back({ current_index, c.offset, c.old_ch, c.new_ch });
							 ++result.replaced;
						 });
	yofy.set_undo_hook(revert_last);
	// Хуки ссылаются на локальные переменные — снимаем их на выходе.
	struct HookGuard
	{
		Yoficator& y;
		~HookGuard()
		{
			y.set_change_hook({});
			y.set_undo_hook({});
		}
	} hook_guard{ yofy };

	bool any_change = false;
	std::size_t changed_lines = 0;
	std::size_t done = 0;
	for (std::size_t i = first; i <= last && i < buf.line_count(); ++i)
	{
		if (on_line)
			on_line(i);
		if (cancel && cancel())
		{
			result.cancelled = true;
			break;
		}
		const std::string_view bytes = buf.line_text(i);
		const UString before = decode_line({ reinterpret_cast<const std::uint8_t*>(bytes.data()), bytes.size() },
										   Encoding::utf8);
		const std::size_t replaced_before = result.replaced;
		current = before;
		current_index = i;
		const bool ok = yofy.process_line(current, i, source, nullptr);
		++result.lines;
		++done;
		if (result.replaced > replaced_before)
			any_change = true;
		UString& text = current;
		line_texts.push_back(result.replaced > replaced_before ? text : UString());
		if (text != before)
		{
			new_lines.push_back(encode_line(text, Encoding::utf8));
			++changed_lines;
		}
		else
		{
			new_lines.push_back(std::string(bytes));
		}
		if (!ok)
		{
			// «Прервать» останавливает проход, оставляя сделанное. «Откат»
			// сюда не попадает: он снимает один шаг и разбор продолжается.
			result.cancelled = true;
			break;
		}
	}

	result.asked = yofy.stats().ask_prompts;
	if (result.rolled_back)
		return result;
	if (!any_change || changed_lines == 0 || done == 0)
		return result;

	// Замена: строки с их собственными EOL между ними; EOL последней
	// строки остаётся в документе (он вне диапазона).
	std::string replacement;
	// Смещение каждой строки внутри replacement — нужно, чтобы перевести
	// замены (кодпоинты в строке) в абсолютные байтовые диапазоны.
	std::vector<std::size_t> line_offsets(new_lines.size() + 1, 0);
	for (std::size_t k = 0; k < new_lines.size(); ++k)
	{
		line_offsets[k] = replacement.size();
		replacement += new_lines[k];
		if (k + 1 < new_lines.size())
			replacement += eol_bytes(buf.line_eol(first + k));
	}
	line_offsets[new_lines.size()] = replacement.size();

	const std::size_t from_off = buf.line_start(first);
	// Пометки — по фактически заменённым символам («Откат» уже убрал свои из
	// edits). Длина замены е↔ё в UTF-8 одинакова, поэтому смещения строк не
	// съезжают и диапазоны остаются верными после вставки replacement.
	for (const Edit& e : edits)
	{
		const std::size_t k = e.line - first;
		if (k >= new_lines.size())
			continue;
		const std::string ch = encode_line(UString(1, e.new_ch), Encoding::utf8);
		const std::size_t byte = from_off + line_offsets[k] +
								 utf8_prefix_bytes(new_lines[k], e.offset);
		result.marks.emplace_back(byte, byte + std::max<std::size_t>(1, ch.size()));
	}
	const std::size_t to_off = buf.line_end(first + new_lines.size() - 1);
	buf.begin_group("Ёфикация");
	buf.erase(from_off, to_off - from_off);
	buf.insert(from_off, replacement);
	buf.end_group();
	result.changed = true;
	return result;
}

YofyResult yofify_document(TextBuffer& buf, std::size_t from, std::size_t to, Yoficator& yofy,
						   std::size_t chunk_lines, const YofyLineFn& on_line,
						   const YofyCancelFn& cancel)
{
	YofyResult total;
	if (buf.size() == 0 || from >= to || chunk_lines == 0)
		return total;

	const std::size_t first = buf.line_of(from);
	to = std::min(to, buf.size());
	const std::size_t last = buf.line_of(to > 0 ? to - 1 : 0);

	// Одна группа на весь проход: вложенные группы кусков её не разрывают.
	buf.begin_group("Ёфикация");
	for (std::size_t line = first; line <= last;)
	{
		const std::size_t next = line + chunk_lines;
		const std::size_t chunk_from = buf.line_start(line);
		const std::size_t chunk_to = next <= last ? buf.line_start(next) : to;
		const YofyResult part = yofify_range(buf, chunk_from, chunk_to, yofy, on_line, cancel);
		total.replaced += part.replaced;
		total.lines += part.lines;
		total.asked += part.asked;
		total.reverted += part.reverted;
		total.marks.insert(total.marks.end(), part.marks.begin(), part.marks.end());
		total.changed = total.changed || part.changed;
		total.rolled_back = total.rolled_back || part.rolled_back;
		if (part.cancelled)
		{
			total.cancelled = true;
			break;
		}
		if (part.lines == 0)
			break; // защита от зацикливания: кусок ничего не обработал
		line = next;
	}
	buf.end_group();
	return total;
}

} // namespace yo
