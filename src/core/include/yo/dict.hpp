// Основной словарь yox.dat: загрузка, валидация, поиск словоформы.
//
// Формат и порядок сравнения — как в оригинале (FORMATS.md §1):
// CP1251, CRLF, '#'-комментарии, завершающий '?' = «спрашивать»,
// сортировка CmpYe (сравнение ToYe + LC, затем байтовый тай-брейк).
#pragma once

#include "yo/cyr.hpp"

#include <cstdint>
#include <expected>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace yo {

struct LoadIssue
{
	std::string kind;	 // "disorder", "bad_word", "no_yo", "io"
	std::string detail;	 // слово или сообщение
};

struct DictLoadResult;

class Dictionary
{
public:
	struct Entry
	{
		std::string ye_raw;	  // словоформа с «е» (CP1251)
		std::string ye_lc;	  // она же в нижнем регистре (Cmp)
		std::string yo_raw;	  // словоформа с «ё»
		std::string yo_lc;	  // она же в нижнем регистре (для статистики)
	};

	Dictionary() = default;

	// Загрузка. Возвращает ошибку только при I/O; нарушения порядка/формы
	// возвращаются в issues (оригинал показывает их как сообщения, но грузит).
	static std::expected<Dictionary, std::string> load(const std::filesystem::path& path,
													   std::vector<LoadIssue>* issues = nullptr);

	[[nodiscard]] std::size_t size() const noexcept { return entries_.size(); }
	[[nodiscard]] bool empty() const noexcept { return entries_.empty(); }

	// FindInStringsUL: сначала «первая буква как есть + остальное LC»,
	// затем полностью нижний регистр. -1 — не найдено.
	[[nodiscard]] int find(UStringView word) const;
	[[nodiscard]] int find_key(std::string_view cp1251_word) const;

	[[nodiscard]] const std::string& version() const noexcept { return version_; }
	[[nodiscard]] UString ye_unicode(std::size_t i) const;
	[[nodiscard]] UString yo_unicode(std::size_t i) const;
	// Те же словоформы, но без копирования: срез общего пула (UTF-32).
	[[nodiscard]] UStringView ye_view(std::size_t i) const noexcept
	{
		const std::size_t k = 2 * i;
		return UStringView(pool_.data() + off_[k], off_[k + 1] - off_[k]);
	}
	[[nodiscard]] UStringView yo_view(std::size_t i) const noexcept
	{
		const std::size_t k = 2 * i + 1;
		return UStringView(pool_.data() + off_[k], off_[k + 1] - off_[k]);
	}
	// Нижний регистр словоформ в CP1251 (сравнение без декодирования).
	[[nodiscard]] std::string_view ye_lc(std::size_t i) const noexcept
	{
		return entries_[i].ye_lc;
	}
	[[nodiscard]] std::string_view yo_lc(std::size_t i) const noexcept
	{
		return entries_[i].yo_lc;
	}

	[[nodiscard]] int ask(std::size_t i) const noexcept { return ask_[i]; }
	[[nodiscard]] int user_sel(std::size_t i) const noexcept { return user_sel_[i]; }
	void set_user_sel(std::size_t i, int v) noexcept { user_sel_[i] = static_cast<std::int8_t>(v); }
	[[nodiscard]] int words_answer(std::size_t i) const noexcept { return words_answer_[i]; }
	void set_words_answer(std::size_t i, int v) noexcept
	{
		words_answer_[i] = static_cast<std::int8_t>(v);
	}

	void reset_user_sel() noexcept;
	// Есть ли хоть один запомненный ответ «Всегда Е/Ё» (для состояния пункта
	// «Сбросить "Всегда Е/Ё"», как SetMenuItem('Clearusersel1') оригинала).
	[[nodiscard]] bool has_user_sel() const noexcept;

	// Счётчики замен по словарю (Ye2Ye, Ye2Yo, Yo2Ye, Yo2Yo).
	struct WordStat
	{
		std::uint64_t ye2ye = 0;
		std::uint64_t ye2yo = 0;
		std::uint64_t yo2ye = 0;
		std::uint64_t yo2yo = 0;
	};
	// Учёт исхода по словоформе (Ye2Ye/Ye2Yo/Yo2Ye/Yo2Yo) — как AddToStat.
	void note_outcome(std::size_t i, bool old_is_ye, bool old_is_yo, bool new_is_ye,
					  bool new_is_yo) noexcept;
	[[nodiscard]] const WordStat& word_stat(std::size_t i) const noexcept { return stats_[i]; }

private:
	std::vector<Entry> entries_;
	std::vector<std::int8_t> ask_;
	std::vector<std::int8_t> user_sel_;
	std::vector<std::int8_t> words_answer_;
	std::vector<WordStat> stats_;
	// Компактный индекс для двоичного поиска: префикс LC-ключа (8 байт),
	// длина и смещение сырого ключа в key_pool_. Порядок совпадает с entries_.
	struct SearchEntry
	{
		std::uint64_t lc8 = 0;
		std::uint32_t len = 0;
		std::uint32_t off = 0;
	};
	std::vector<SearchEntry> idx_;
	std::vector<char> key_pool_;
	// Хеш-таблица по LC-ключу (открытая адресация): 0xFFFFFFFF — пусто.
	// Если в словаре есть совпадающие LC-ключи, поиск идёт двоичным
	// (точная последовательность оригинала), чтобы не менять выбор записи.
	std::vector<std::uint32_t> hash_;
	// LC-ключи, встречающиеся в словаре более одного раза: для них выбор
	// записи повторяет двоичный поиск оригинала.
	std::vector<std::string> dup_lc_;
	[[nodiscard]] int find_key_bsearch(std::string_view key) const;
	// Декодированные словоформы (е-форма, затем ё-форма) в одном пуле:
	// индексы off_ идут тройками [е_i, ё_i, е_{i+1}, ...], размер 2n+1.
	std::vector<char32_t> pool_;
	std::vector<std::uint32_t> off_;
	std::string version_;
};

} // namespace yo
