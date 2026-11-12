#include "yo/yofytext.hpp"

#include "yo/codec.hpp"
#include "yo/utf.hpp"

#include <string>
#include <vector>

namespace yo {

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
		UString text = before;
		std::vector<Change> changes;
		const bool ok = yofy.process_line(text, i, source, &changes);
		++result.lines;
		++done;
		result.replaced += changes.size();
		if (!changes.empty())
			any_change = true;
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
			// «Откат» отменяет весь проход, «Прервать» оставляет сделанное.
			result.rolled_back = yofy.undo_requested();
			result.cancelled = !result.rolled_back;
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
	for (std::size_t k = 0; k < new_lines.size(); ++k)
	{
		replacement += new_lines[k];
		if (k + 1 < new_lines.size())
			replacement += eol_bytes(buf.line_eol(first + k));
	}

	const std::size_t from_off = buf.line_start(first);
	const std::size_t to_off = buf.line_end(first + new_lines.size() - 1);
	buf.begin_group("Ёфикация");
	buf.erase(from_off, to_off - from_off);
	buf.insert(from_off, replacement);
	buf.end_group();
	result.changed = true;
	return result;
}

} // namespace yo
