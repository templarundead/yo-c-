// Текстовый буфер документа: piece table поверх неизменяемого источника.
//
// - исходный текст (mmap) не копируется и не изменяется;
// - правки живут в отдельной арене, куски ссылаются либо в источник,
//   либо в арену;
// - смещения — байтовые в UTF-8 (канонические, см. ARCHITECTURE.md §2.2);
// - индекс строк поддерживается инкрементально (границы CRLF/LF/CR);
// - undo/redo — обратные операции, сгруппированные в транзакции.
#pragma once

#include "yo/mapped.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace yo {

enum class Eol : std::uint8_t { none, lf, crlf, cr };

class TextBuffer
{
public:
	TextBuffer();
	explicit TextBuffer(std::shared_ptr<const ByteSource> source);

	TextBuffer(const TextBuffer&) = delete;
	TextBuffer& operator=(const TextBuffer&) = delete;
	TextBuffer(TextBuffer&&) noexcept = default;
	TextBuffer& operator=(TextBuffer&&) noexcept = default;

	[[nodiscard]] std::size_t size() const noexcept { return total_; }
	[[nodiscard]] bool empty() const noexcept { return total_ == 0; }
	[[nodiscard]] std::size_t line_count() const noexcept { return starts_.size(); }

	// Границы строки: start — смещение начала, end — конец без EOL.
	[[nodiscard]] std::size_t line_start(std::size_t line) const;
	[[nodiscard]] std::size_t line_end(std::size_t line) const;
	[[nodiscard]] Eol line_eol(std::size_t line) const;
	// Строка, содержащая байтовое смещение (offset == size → последняя).
	[[nodiscard]] std::size_t line_of(std::size_t offset) const;

	// Текст строки без EOL (копия во внутренний буфер).
	// ВАЖНО: вид живёт до следующего вызова line_text/read_chunk — буфер общий.
	// Хранить его дольше одного выражения нельзя: копируйте (std::string).
	[[nodiscard]] std::string_view line_text(std::size_t line) const;

	// Копия произвольного диапазона.
	void read(std::size_t offset, std::size_t len, std::string& out) const;
	[[nodiscard]] std::string text() const;

	// Правки (границы кодпоинтов UTF-8 проверяет вызывающий).
	void insert(std::size_t offset, std::string_view text);
	void erase(std::size_t offset, std::size_t len);

	// Undo/redo. Правки без begin_group/end_group образуют отдельный шаг.
	void begin_group(std::string_view name = {});
	void end_group();
	[[nodiscard]] bool can_undo() const noexcept { return !undo_.empty(); }
	[[nodiscard]] bool can_redo() const noexcept { return !redo_.empty(); }
	bool undo();
	bool redo();
	[[nodiscard]] bool modified() const noexcept { return modified_; }
	void mark_saved() noexcept { modified_ = false; }
	[[nodiscard]] std::size_t undo_depth() const noexcept { return undo_.size(); }

	// Весь текст в out (UTF-8).
	void write_all(std::string& out) const;

	[[nodiscard]] const ByteSource* source() const noexcept { return source_.get(); }
	// Правок нет: содержимое совпадает с источником.
	[[nodiscard]] bool is_pristine() const noexcept { return arena_.empty(); }

private:
	struct Piece
	{
		std::size_t off = 0;   // смещение в источнике или в арене
		std::size_t len = 0;
		bool from_source = true;
	};
	struct Op
	{
		std::size_t offset = 0;
		std::string erased;
		std::string inserted;
	};
	struct Group
	{
		std::string name;
		std::vector<Op> ops;
	};

	[[nodiscard]] const char* base(bool from_source) const noexcept
	{
		return from_source ? (source_ ? source_->data() : nullptr) : arena_.data();
	}
	[[nodiscard]] std::string_view piece_view(const Piece& p) const noexcept
	{
		const char* b = base(p.from_source);
		return b == nullptr ? std::string_view{} : std::string_view(b + p.off, p.len);
	}

	void rebuild_starts() const;
	[[nodiscard]] std::size_t piece_index(std::size_t offset, std::size_t& in_piece) const;
	void normalize_pieces();
	void splice_pieces(std::size_t offset, std::size_t erase_len, std::string_view insert_text);

	void index_after_edit(std::size_t line_a, std::size_t a, std::size_t j_old,
						  std::size_t len_new, std::size_t len_old, std::string_view unused);
	[[nodiscard]] std::size_t first_line_start_at_least(std::size_t offset) const;
	std::vector<std::size_t> boundaries_in(std::size_t from, std::size_t to) const;
	// Ближайшая граница строки после pos (поиск ограничен kMaxBoundaryScan
	// байтами; если границы нет — total_).
	[[nodiscard]] std::size_t next_boundary_after(std::size_t pos) const;
	[[nodiscard]] char byte_at(std::size_t offset) const;

	// Правки без записи в undo (используются и отменой/повтором).
	void apply_insert(std::size_t offset, std::string_view text);
	void apply_erase(std::size_t offset, std::size_t len);

	void record(std::size_t offset, std::string erased, std::string inserted);
	void clear_redo() noexcept
	{
		redo_.clear();
	}

	std::shared_ptr<const ByteSource> source_;
	std::string arena_;
	std::vector<Piece> pieces_;
	std::size_t total_ = 0;

	mutable std::vector<std::size_t> starts_;  // начала строк, [0] == 0
	mutable std::vector<std::size_t> pstarts_; // префиксные смещения кусков
	mutable bool pstarts_dirty_ = true;
	mutable std::string scratch_;			   // для line_text

	std::vector<Group> undo_;
	std::vector<Group> redo_;
	Group pending_;
	bool in_group_ = false;
	int group_depth_ = 0; // вложенные begin_group: правки всё равно в одну группу
	bool modified_ = false;
};

} // namespace yo
