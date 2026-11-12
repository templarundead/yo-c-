#include "yo/textbuf.hpp"

#include <algorithm>
#include <cassert>

namespace yo {

namespace {

// Предел памяти под undo: старые группы отбрасываются.
constexpr std::size_t kMaxUndoGroups = 1000;

// Сколько байт просматривать вперёд в поисках границы строки после правки.
// Найденная граница ограничивает окно пересборки индекса; если её нет
// (очень длинная строка), окном становится весь остаток текста.
constexpr std::size_t kMaxBoundaryScan = 1u << 16;

} // namespace

TextBuffer::TextBuffer()
{
	starts_.assign(1, 0);
	pstarts_.assign(1, 0);
}

TextBuffer::TextBuffer(std::shared_ptr<const ByteSource> source) : source_(std::move(source))
{
	const std::size_t n = source_ ? source_->size() : 0;
	if (n > 0)
	{
		pieces_.push_back({ 0, n, true });
		total_ = n;
	}
	pstarts_dirty_ = true;
	// Индекс строится один раз по источнику (для больших файлов — в фоне,
	// см. ARCHITECTURE.md §2.3).
	starts_.clear();
	starts_.push_back(0);
	for (const std::size_t b : boundaries_in(0, total_))
		starts_.push_back(b);
}

void TextBuffer::rebuild_starts() const
{
	pstarts_dirty_ = true;
}

std::size_t TextBuffer::piece_index(std::size_t offset, std::size_t& in_piece) const
{
	if (pstarts_dirty_)
	{
		pstarts_.assign(pieces_.size() + 1, 0);
		for (std::size_t i = 0; i < pieces_.size(); ++i)
			pstarts_[i + 1] = pstarts_[i] + pieces_[i].len;
		pstarts_dirty_ = false;
	}
	if (pieces_.empty())
	{
		in_piece = 0;
		return 0;
	}
	const auto it = std::upper_bound(pstarts_.begin(), pstarts_.end(), offset);
	std::size_t i = static_cast<std::size_t>(it - pstarts_.begin());
	if (i == 0)
		i = 1;
	i -= 1;
	if (i >= pieces_.size())
		i = pieces_.size() - 1;
	in_piece = offset - pstarts_[i];
	return i;
}

char TextBuffer::byte_at(std::size_t offset) const
{
	if (offset >= total_)
		return '\0';
	std::size_t in = 0;
	const std::size_t i = piece_index(offset, in);
	const std::string_view v = piece_view(pieces_[i]);
	return in < v.size() ? v[in] : '\0';
}

std::vector<std::size_t> TextBuffer::boundaries_in(std::size_t from, std::size_t to) const
{
	std::vector<std::size_t> out;
	to = std::min(to, total_);
	if (from >= to)
		return out;

	// Кэш куска: соседние байты почти всегда лежат в одном куске, поэтому
	// поиск куска по смещению выполняется лишь на границах кусков.
	std::size_t ci = pieces_.size();
	std::size_t cstart = 0;
	std::string_view cv;
	auto byte = [&](std::size_t off) -> char {
		if (off >= total_)
			return '\0';
		if (ci >= pieces_.size() || off < cstart || off - cstart >= cv.size())
		{
			std::size_t in = 0;
			ci = piece_index(off, in);
			cstart = off - in;
			cv = piece_view(pieces_[ci]);
		}
		return cv[off - cstart];
	};

	std::size_t pos = from;
	while (pos < to)
	{
		const char c = byte(pos);
		if (c == '\r')
		{
			// CRLF распознаётся по следующему байту, даже если он в другом
			// куске; в конце текста это одиночный CR.
			if (byte(pos + 1) == '\n')
			{
				if (pos + 2 <= to)
					out.push_back(pos + 2);
				pos += 2;
			}
			else
			{
				out.push_back(pos + 1);
				++pos;
			}
			continue;
		}
		if (c == '\n')
		{
			out.push_back(pos + 1);
			++pos;
			continue;
		}
		++pos;
	}
	return out;
}

std::size_t TextBuffer::next_boundary_after(std::size_t pos) const
{
	const std::size_t limit = std::min(total_, pos + kMaxBoundaryScan);
	for (std::size_t p = pos; p < limit; ++p)
	{
		const char c = byte_at(p);
		if (c == '\n')
			return p + 1;
		if (c == '\r')
			return byte_at(p + 1) == '\n' ? p + 2 : p + 1;
	}
	return total_;
}

std::size_t TextBuffer::line_start(std::size_t line) const
{
	if (starts_.empty())
		return 0;
	if (line >= starts_.size())
		return total_;
	return starts_[line];
}

std::size_t TextBuffer::line_end(std::size_t line) const
{
	const std::size_t s = line_start(line);
	std::size_t e = line + 1 < starts_.size() ? starts_[line + 1] : total_;
	if (e <= s)
		return s;
	// Снять EOL.
	if (e >= 2 && byte_at(e - 1) == '\n' && byte_at(e - 2) == '\r')
		return e - 2;
	if (byte_at(e - 1) == '\n' || byte_at(e - 1) == '\r')
		return e - 1;
	return e;
}

Eol TextBuffer::line_eol(std::size_t line) const
{
	const std::size_t s = line_start(line);
	const std::size_t e = line + 1 < starts_.size() ? starts_[line + 1] : total_;
	// Пустая последняя строка (конец файла сразу после EOL) — без EOL.
	if (e <= s)
		return Eol::none;
	const char last = byte_at(e - 1);
	if (last == '\n')
		return (e >= 2 && byte_at(e - 2) == '\r') ? Eol::crlf : Eol::lf;
	if (last == '\r')
		return Eol::cr;
	return Eol::none;
}

std::size_t TextBuffer::line_of(std::size_t offset) const
{
	if (starts_.empty())
		return 0;
	const auto it = std::upper_bound(starts_.begin(), starts_.end(), offset);
	std::size_t i = static_cast<std::size_t>(it - starts_.begin());
	if (i == 0)
		return 0;
	return i - 1;
}

std::size_t TextBuffer::first_line_start_at_least(std::size_t offset) const
{
	const auto it = std::lower_bound(starts_.begin(), starts_.end(), offset);
	return static_cast<std::size_t>(it - starts_.begin());
}

std::string_view TextBuffer::line_text(std::size_t line) const
{
	const std::size_t l = line_start(line);
	const std::size_t r = line_end(line);
	scratch_.clear();
	if (r > l)
		read(l, r - l, scratch_);
	return scratch_;
}

void TextBuffer::read(std::size_t offset, std::size_t len, std::string& out) const
{
	out.clear();
	if (len == 0 || offset >= total_)
		return;
	const std::size_t end = std::min(offset + len, total_);
	std::size_t in = 0;
	std::size_t i = piece_index(offset, in);
	out.reserve(end - offset);
	std::size_t pos = offset;
	while (pos < end && i < pieces_.size())
	{
		const std::string_view v = piece_view(pieces_[i]);
		const std::size_t avail = v.size() - in;
		const std::size_t take = std::min(avail, end - pos);
		out.append(v.data() + in, take);
		pos += take;
		++i;
		in = 0;
	}
}

std::string TextBuffer::text() const
{
	std::string out;
	out.reserve(total_);
	write_all(out);
	return out;
}

void TextBuffer::write_all(std::string& out) const
{
	out.clear();
	out.reserve(total_);
	for (const Piece& p : pieces_)
	{
		const std::string_view v = piece_view(p);
		out.append(v.data(), v.size());
	}
}

void TextBuffer::normalize_pieces()
{
	// Склеиваем соседние куски из одного источника, чтобы куски не росли.
	std::vector<Piece> merged;
	merged.reserve(pieces_.size());
	for (const Piece& p : pieces_)
	{
		if (p.len == 0)
			continue;
		if (!merged.empty())
		{
			Piece& last = merged.back();
			if (last.from_source == p.from_source && last.off + last.len == p.off)
			{
				last.len += p.len;
				continue;
			}
		}
		merged.push_back(p);
	}
	if (merged.empty())
		merged.push_back({ 0, 0, true });
	pieces_ = std::move(merged);
	pstarts_dirty_ = true;
}

void TextBuffer::splice_pieces(std::size_t offset, std::size_t erase_len, std::string_view insert_text)
{
	// 1. Разрезать кусок на границе вставки.
	std::size_t in = 0;
	std::size_t i = piece_index(offset, in);
	std::vector<Piece> mid;	 // новые куски на месте правки

	if (erase_len == 0)
	{
		if (i < pieces_.size() && in < pieces_[i].len)
		{
			// Разрез куска: вставка идёт между двумя половинами.
			const Piece p = pieces_[i];
			pieces_[i] = { p.off, in, p.from_source };
			pieces_.insert(pieces_.begin() + static_cast<std::ptrdiff_t>(i) + 1,
						   { p.off + in, p.len - in, p.from_source });
			++i;
		}
		else
		{
			i = std::min(i + 1, pieces_.size());
		}
		if (!insert_text.empty())
		{
			const std::size_t arena_off = arena_.size();
			arena_.append(insert_text);
			pieces_.insert(pieces_.begin() + static_cast<std::ptrdiff_t>(i),
						   { arena_off, insert_text.size(), false });
			total_ += insert_text.size();
		}
		pstarts_dirty_ = true;
		normalize_pieces();
		return;
	}

	// 2. Удаление: вырезать [offset, offset+erase_len) по кускам.
	const std::size_t end = offset + erase_len;
	std::size_t pos = offset;
	i = piece_index(offset, in);
	std::vector<Piece> out;
	out.reserve(pieces_.size() + 2);
	for (std::size_t k = 0; k < i; ++k)
		out.push_back(pieces_[k]);
	std::size_t idx = i;
	std::size_t cur_in = in;
	bool inserted = false;
	while (pos < end && idx < pieces_.size())
	{
		const Piece p = pieces_[idx];
		const std::size_t piece_len = p.len;
		const std::size_t take = std::min(piece_len - cur_in, end - pos);
		if (cur_in > 0)
			out.push_back({ p.off, cur_in, p.from_source });
		// вставка ставится один раз, на месте вырезанного диапазона
		if (!inserted && !insert_text.empty())
		{
			const std::size_t arena_off = arena_.size();
			arena_.append(insert_text);
			out.push_back({ arena_off, insert_text.size(), false });
			inserted = true;
		}
		const std::size_t rest_off = p.off + cur_in + take;
		const std::size_t rest_len = piece_len - cur_in - take;
		pos += take;
		if (rest_len > 0)
			out.push_back({ rest_off, rest_len, p.from_source });
		cur_in = 0;
		++idx;
	}
	for (std::size_t k = idx; k < pieces_.size(); ++k)
		out.push_back(pieces_[k]);

	if (!inserted && !insert_text.empty())
	{
		const std::size_t arena_off = arena_.size();
		arena_.append(insert_text);
		out.push_back({ arena_off, insert_text.size(), false });
	}

	pieces_ = std::move(out);
	total_ = total_ - erase_len + insert_text.size();
	pstarts_dirty_ = true;
	normalize_pieces();
}

void TextBuffer::index_after_edit(std::size_t line_a, std::size_t a, std::size_t j_old,
								  std::size_t len_new, std::size_t len_old, std::string_view)
{
	const std::ptrdiff_t delta = static_cast<std::ptrdiff_t>(len_new) -
								 static_cast<std::ptrdiff_t>(len_old);
	// Конец правки в новых координатах: j_old + delta (при удалении len_new=0).
	const std::size_t j_new = static_cast<std::size_t>(static_cast<std::ptrdiff_t>(j_old) + delta);

	// Первая граница строки после конца правки (k >= 1): суффикс за ней
	// не изменился, поэтому граница существует и в старых координатах.
	// Поиск ограничен окном, иначе каждая правка стоила бы O(размера текста).
	const std::size_t p_new = next_boundary_after(j_new);
	const std::size_t k = p_new - j_new;
	const std::size_t q_old = j_old + k;

	const std::vector<std::size_t> new_bounds = boundaries_in(a, p_new);
	const std::size_t lo = line_a + 1;
	// q_old уже учтён в new_bounds (последняя граница окна), поэтому хвост
	// начинается со следующей за ним старой границы.
	const std::size_t hi = std::max(lo, first_line_start_at_least(q_old + 1));

	std::vector<std::size_t> tail;
	if (starts_.size() > hi)
		tail.assign(starts_.begin() + static_cast<std::ptrdiff_t>(hi), starts_.end());

	starts_.resize(std::min(lo, starts_.size()));
	starts_.insert(starts_.end(), new_bounds.begin(), new_bounds.end());
	// Сдвиг хвоста и вставка «на место», без прохода по всему индексу:
	// иначе каждая правка стоила бы O(числа строк документа).
	for (std::size_t& s : tail)
		s = static_cast<std::size_t>(static_cast<std::ptrdiff_t>(s) + delta);
	std::size_t skip = 0;
	// Граница, созданная правкой, может совпасть со сдвинутой старой
	// (p_new не всегда происходит из старой границы): дубль на стыке
	// отбрасываем — массив обязан строго возрастать.
	if (!tail.empty() && !starts_.empty() && tail.front() == starts_.back())
		skip = 1;
	starts_.insert(starts_.end(), tail.begin() + static_cast<std::ptrdiff_t>(skip), tail.end());
	if (starts_.empty())
		starts_.push_back(0);
}

void TextBuffer::record(std::size_t offset, std::string erased, std::string inserted)
{
	if (!pending_.ops.empty() && !in_group_)
	{
		// Каждая правка вне группы — отдельный шаг undo.
		undo_.push_back(std::move(pending_));
		pending_ = Group{};
	}
	pending_.ops.push_back({ offset, std::move(erased), std::move(inserted) });
	if (!in_group_)
	{
		undo_.push_back(std::move(pending_));
		pending_ = Group{};
	}
	if (undo_.size() > kMaxUndoGroups)
		undo_.erase(undo_.begin());
	redo_.clear();
}

void TextBuffer::begin_group(std::string_view name)
{
	// Вложенные группы (например, ёфикация документа кусками, где каждый
	// кусок открывает свою) — одна группа undo: пользователю нужен один
	// «Отменить» на весь проход.
	if (in_group_)
	{
		++group_depth_;
		return;
	}
	in_group_ = true;
	group_depth_ = 0;
	pending_ = Group{};
	pending_.name = std::string(name);
}

void TextBuffer::end_group()
{
	if (!in_group_)
		return;
	if (group_depth_ > 0)
	{
		--group_depth_;
		return;
	}
	in_group_ = false;
	if (!pending_.ops.empty())
	{
		undo_.push_back(std::move(pending_));
		if (undo_.size() > kMaxUndoGroups)
			undo_.erase(undo_.begin());
	}
	pending_ = Group{};
}

void TextBuffer::insert(std::size_t offset, std::string_view text)
{
	if (text.empty() || offset > total_)
		return;
	apply_insert(offset, text);
	record(offset, {}, std::string(text));
	modified_ = true;
}

void TextBuffer::erase(std::size_t offset, std::size_t len)
{
	if (len == 0 || offset >= total_)
		return;
	len = std::min(len, total_ - offset);
	std::string erased;
	read(offset, len, erased);
	apply_erase(offset, len);
	record(offset, std::move(erased), {});
	modified_ = true;
}

void TextBuffer::apply_insert(std::size_t offset, std::string_view text)
{
	if (text.empty() || offset > total_)
		return;
	// Окно пересборки начинается со старта предыдущей строки: правка у самого
	// начала строки может убрать границу (вставка '\n' после одиночного '\r'
	// превращает его в CRLF), поэтому строка-«якорь» тоже пересчитывается.
	const std::size_t line_a = line_of(offset);
	const std::size_t base_line = line_a == 0 ? 0 : line_a - 1;
	const std::size_t base = starts_.empty() ? 0 : starts_[base_line];
	splice_pieces(offset, 0, text);
	index_after_edit(base_line, base, offset, text.size(), 0, {});
}

void TextBuffer::apply_erase(std::size_t offset, std::size_t len)
{
	if (len == 0 || offset >= total_)
		return;
	len = std::min(len, total_ - offset);
	const std::size_t line_a = line_of(offset);
	const std::size_t base_line = line_a == 0 ? 0 : line_a - 1;
	const std::size_t base = starts_.empty() ? 0 : starts_[base_line];
	splice_pieces(offset, len, {});
	index_after_edit(base_line, base, offset + len, 0, len, {});
}

bool TextBuffer::undo()
{
	if (undo_.empty())
		return false;
	Group g = std::move(undo_.back());
	undo_.pop_back();
	for (auto it = g.ops.rbegin(); it != g.ops.rend(); ++it)
	{
		const Op& op = *it;
		if (!op.inserted.empty())
			apply_erase(op.offset, op.inserted.size());
		if (!op.erased.empty())
			apply_insert(op.offset, op.erased);
	}
	redo_.push_back(std::move(g));
	modified_ = true;
	return true;
}

bool TextBuffer::redo()
{
	if (redo_.empty())
		return false;
	Group g = std::move(redo_.back());
	redo_.pop_back();
	for (const Op& op : g.ops)
	{
		if (!op.erased.empty())
			apply_erase(op.offset, op.erased.size());
		if (!op.inserted.empty())
			apply_insert(op.offset, op.inserted);
	}
	undo_.push_back(std::move(g));
	modified_ = true;
	return true;
}

} // namespace yo
