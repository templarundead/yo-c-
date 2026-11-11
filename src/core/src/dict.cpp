#include "yo/dict.hpp"

#include "yo/cp1251.hpp"

#include <algorithm>
#include <fstream>

namespace yo {

namespace {

// AnsiLowerCase для одного байта CP1251 — без выделения памяти.
inline char lower_byte(char ch)
{
	const auto c = static_cast<unsigned char>(ch);
	if (c >= 'A' && c <= 'Z')
		return static_cast<char>(c + 32);
	if (c >= 0xC0 && c <= 0xDF)
		return static_cast<char>(c + 32);
	if (c == 0xA8)
		return static_cast<char>(0xB8);
	return ch;
}


bool is_comment(std::string_view s)
{
	return s.empty() || s.front() == '#';
}

// Cmp: сначала LC-сравнение, затем байтовый тай-брейк по исходным строкам.
int cmp_keys(std::string_view a_raw, std::string_view a_lc, std::string_view b_raw,
			 std::string_view b_lc)
{
	const int c = a_lc.compare(b_lc);
	if (c != 0)
		return c < 0 ? -1 : 1;
	const std::size_t n = a_raw.size();
	for (std::size_t i = 0; i < n; ++i)
	{
		const auto ca = static_cast<unsigned char>(i < a_raw.size() ? a_raw[i] : 0);
		const auto cb = static_cast<unsigned char>(i < b_raw.size() ? b_raw[i] : 0);
		if (ca != cb)
			return ca > cb ? 1 : -1;
	}
	return 0;
}

bool is_word_key(std::string_view key)
{
	// IsWord: все символы — кириллические буквы (Ё/ё включительно).
	return std::ranges::all_of(key, [](char ch)
							   {
								   return cyr::is_alpha(
									   cp1251::to_unicode(static_cast<std::uint8_t>(
										   static_cast<unsigned char>(ch))));
							   });
}

bool key_has_yo(std::string_view key)
{
	return std::ranges::any_of(key, [](char ch)
							   {
								   return cyr::is_yo(
									   cp1251::to_unicode(static_cast<std::uint8_t>(
										   static_cast<unsigned char>(ch))));
							   });
}

// ToYe для ключа CP1251.
std::string to_ye_key(std::string_view key)
{
	std::string out(key);
	for (char& ch : out)
	{
		const auto c = cp1251::to_unicode(static_cast<std::uint8_t>(static_cast<unsigned char>(ch)));
		if (cyr::is_yo(c))
		{
			const auto mapped = cp1251::from_unicode(cyr::is_upper(c) ? cyr::kYeUpper : cyr::kYeLower);
			ch = static_cast<char>(*mapped);
		}
	}
	return out;
}

std::string trim(std::string_view s)
{
	std::size_t b = 0;
	std::size_t e = s.size();
	while (b < e && (s[b] == ' ' || s[b] == '\t' || s[b] == '\r' || s[b] == '\n'))
		++b;
	while (e > b && (s[e - 1] == ' ' || s[e - 1] == '\t' || s[e - 1] == '\r' || s[e - 1] == '\n'))
		--e;
	return std::string(s.substr(b, e - b));
}

} // namespace

std::expected<Dictionary, std::string> Dictionary::load(const std::filesystem::path& path,
														std::vector<LoadIssue>* issues)
{
	std::ifstream in(path, std::ios::binary);
	if (!in)
		return std::unexpected("не удалось открыть словарь: " + path.string());

	Dictionary dict;
	std::string line;
	std::string prev_ye;
	std::string first_disorder;
	std::string first_bad_word;
	std::string first_no_yo;

	auto add_issue = [&](std::string kind, std::string detail)
	{
		if (issues != nullptr)
			issues->push_back({ std::move(kind), std::move(detail) });
	};

	while (std::getline(in, line))
	{
		if (!line.empty() && line.back() == '\r')
			line.pop_back();
		if (is_comment(line))
		{
			if (dict.version_.empty())
			{
				// GetDicVersion: текст после «Версия:» без пробелов по краям.
				const std::string marker = cp1251::encode(U"Версия:");
				const auto p = line.find(marker);
				if (p != std::string::npos)
					dict.version_ = trim(std::string_view(line).substr(p + marker.size()));
			}
			continue;
		}

		std::string yo = line;
		std::int8_t ask = 0;
		if (!yo.empty() && yo.back() == '?')
		{
			yo.pop_back();
			ask = 1;
		}
		std::string ye = to_ye_key(yo);

		if (first_bad_word.empty() && !is_word_key(ye))
			first_bad_word = yo;
		if (first_disorder.empty() && cmp_keys(ye, cp1251::lower_key(ye), prev_ye,
											   cp1251::lower_key(prev_ye)) <= 0)
			first_disorder = yo;
		if (first_no_yo.empty() && !key_has_yo(yo))
			first_no_yo = yo;

		Entry e;
		e.ye_raw = ye;
		e.ye_lc = cp1251::lower_key(ye);
		e.yo_raw = yo;
		e.yo_lc = cp1251::lower_key(yo);
		dict.entries_.push_back(std::move(e));
		dict.ask_.push_back(ask);
		dict.user_sel_.push_back(0);
		dict.words_answer_.push_back(0);
		dict.stats_.emplace_back();
		prev_ye = ye;
	}

	if (!first_disorder.empty())
		add_issue("disorder", first_disorder);
	if (!first_bad_word.empty())
		add_issue("bad_word", first_bad_word);
	if (!first_no_yo.empty())
		add_issue("no_yo", first_no_yo);

	// Пул декодированных словоформ: движок получает срезы без копирования.
	dict.pool_.clear();
	dict.off_.clear();
	dict.off_.reserve(2 * dict.entries_.size() + 1);
	dict.off_.push_back(0);
	for (const auto& e : dict.entries_)
	{
		const UString ye = cp1251::decode(e.ye_raw);
		dict.pool_.insert(dict.pool_.end(), ye.begin(), ye.end());
		dict.off_.push_back(static_cast<std::uint32_t>(dict.pool_.size()));
		const UString yo = cp1251::decode(e.yo_raw);
		dict.pool_.insert(dict.pool_.end(), yo.begin(), yo.end());
		dict.off_.push_back(static_cast<std::uint32_t>(dict.pool_.size()));
	}

	// Компактный индекс поиска: 16 байт на словоформу вместо четырёх строк.
	dict.idx_.clear();
	dict.key_pool_.clear();
	dict.idx_.reserve(dict.entries_.size());
	std::size_t pool_size = 0;
	for (const auto& e : dict.entries_)
		pool_size += e.ye_raw.size();
	dict.key_pool_.reserve(pool_size);
	for (const auto& e : dict.entries_)
	{
		Dictionary::SearchEntry se;
		// Префикс всегда 8 байт: недостающие байты — нули (как у запроса).
		std::uint64_t v = 0;
		for (std::size_t k = 0; k < 8; ++k)
			v = (v << 8) | static_cast<unsigned char>(k < e.ye_raw.size()
															  ? lower_byte(e.ye_raw[k])
															  : 0);
		se.lc8 = v;
		se.len = static_cast<std::uint32_t>(e.ye_raw.size());
		se.off = static_cast<std::uint32_t>(dict.key_pool_.size());
		dict.key_pool_.insert(dict.key_pool_.end(), e.ye_raw.begin(), e.ye_raw.end());
		dict.idx_.push_back(se);
	}

	// Хеш-таблица по LC-ключу: поиск за одно обращение.
	{
		std::size_t cap = 16;
		while (cap < dict.entries_.size() * 2)
			cap <<= 1;
		dict.hash_.assign(cap, 0xFFFFFFFFu);
		dict.dup_lc_.clear();
		const std::size_t mask = cap - 1;
		for (std::size_t i = 0; i < dict.entries_.size(); ++i)
		{
			const std::string& lc = dict.entries_[i].ye_lc;
			std::uint64_t h = 1469598103934665603ull;
			for (const char ch : lc)
				h = (h ^ static_cast<unsigned char>(ch)) * 1099511628211ull;
			std::size_t slot = static_cast<std::size_t>(h) & mask;
			for (;;)
			{
				const std::uint32_t cur = dict.hash_[slot];
				if (cur == 0xFFFFFFFFu)
				{
					dict.hash_[slot] = static_cast<std::uint32_t>(i);
					break;
				}
				if (cur == 0xFFFFFFFEu)
				{
					// В этой цепочке уже есть дубликат — идём дальше.
					slot = (slot + 1) & mask;
					continue;
				}
				if (dict.entries_[cur].ye_lc == lc)
				{
					// Дубликат LC-ключа: помечаем слот и запоминаем ключ.
					dict.hash_[slot] = 0xFFFFFFFEu;
					const bool known =
						std::ranges::find(dict.dup_lc_, lc) != dict.dup_lc_.end();
					if (!known)
						dict.dup_lc_.push_back(lc);
					break;
				}
				slot = (slot + 1) & mask;
			}
		}
	}

	return dict;
}

namespace {

// Cmp(entry, query): LC-сравнение, затем байтовый тай-брейк.
// first_raw: первая буква запроса сравнивается без понижения регистра
// (поведение RestLowerCase в FindInStringsUL).
// rest_lower: тай-брейк идёт по «RestLowerCase(запрос)» (первая буква как есть),
// иначе — по полностью нижнему регистру. LC-сравнение в обоих случаях
// понижает регистр обеих строк (как Cmp в оригинале).
int cmp_entry(const Dictionary::Entry& e, std::string_view q, bool rest_lower)
{
	const std::size_t n = std::min(e.ye_raw.size(), q.size());
	for (std::size_t i = 0; i < n; ++i)
	{
		const char a = lower_byte(e.ye_raw[i]);
		const char b = lower_byte(q[i]);
		if (a != b)
			return static_cast<unsigned char>(a) < static_cast<unsigned char>(b) ? -1 : 1;
	}
	if (e.ye_raw.size() != q.size())
		return e.ye_raw.size() < q.size() ? -1 : 1;
	for (std::size_t i = 0; i < e.ye_raw.size(); ++i)
	{
		const auto a = static_cast<unsigned char>(e.ye_raw[i]);
		const auto b = static_cast<unsigned char>((i == 0 && rest_lower) ? q[i]
																		: lower_byte(q[i]));
		if (a != b)
			return a < b ? -1 : 1;
	}
	return 0;
}

} // namespace

int Dictionary::find_key(std::string_view key) const
{
	if (!hash_.empty())
	{
		// Ключ понижается в регистре в буфер на стеке (слова короче 64 байт).
		char small[64];
		std::string big;
		std::string_view qlc;
		if (key.size() <= sizeof small)
		{
			for (std::size_t k = 0; k < key.size(); ++k)
				small[k] = lower_byte(key[k]);
			qlc = std::string_view(small, key.size());
		}
		else
		{
			big.resize(key.size());
			for (std::size_t k = 0; k < key.size(); ++k)
				big[k] = lower_byte(key[k]);
			qlc = big;
		}
		std::uint64_t h = 1469598103934665603ull;
		for (const char ch : qlc)
			h = (h ^ static_cast<unsigned char>(ch)) * 1099511628211ull;
		const std::size_t mask = hash_.size() - 1;
		std::size_t slot = static_cast<std::size_t>(h) & mask;
		for (;;)
		{
			const std::uint32_t v = hash_[slot];
			if (v == 0xFFFFFFFFu)
				return -1;
			if (v == 0xFFFFFFFEu)
			{
				// Здесь живут дубликаты: решает точный алгоритм оригинала.
				for (const auto& d : dup_lc_)
					if (std::string_view(d) == qlc)
						return find_key_bsearch(key);
				slot = (slot + 1) & mask;
				continue;
			}
			const Entry& e = entries_[v];
			if (e.ye_lc.size() == qlc.size() &&
				std::string_view(e.ye_lc) == qlc)
				return static_cast<int>(v);
			slot = (slot + 1) & mask;
		}
	}
	return find_key_bsearch(key);
}

int Dictionary::find_key_bsearch(std::string_view key) const
{
	const std::size_t n = idx_.size();
	if (n == 0)
		return -1;
	if (idx_.size() != entries_.size())
	{
		// Индекс не построен (словарь создан вручную): прямой путь.
		const std::size_t m = entries_.size();
		auto find_dumb = [&](bool first_raw) -> int
		{
			for (std::size_t i = 0; i < m; ++i)
				if (cmp_entry(entries_[i], key, first_raw) == 0)
					return static_cast<int>(i);
			return -1;
		};
		const int a = find_dumb(true);
		return a >= 0 ? a : find_dumb(false);
	}

	// Запрос понижается в регистре один раз на поиск (а не на каждую пробу).
	char small[64];
	std::string big;
	std::string_view qlc;
	if (key.size() <= sizeof small)
	{
		for (std::size_t k = 0; k < key.size(); ++k)
			small[k] = lower_byte(key[k]);
		qlc = std::string_view(small, key.size());
	}
	else
	{
		big.resize(key.size());
		for (std::size_t k = 0; k < key.size(); ++k)
			big[k] = lower_byte(key[k]);
		qlc = big;
	}
	std::uint64_t qv = 0;
	for (std::size_t k = 0; k < 8; ++k)
		qv = (qv << 8) | static_cast<unsigned char>(k < qlc.size() ? qlc[k] : 0);

	// Cmp по компактному индексу: те же сравнения, что и cmp_entry,
	// но префикс берётся из индекса (одна строка кэша на пробу).
	auto cmp_idx = [&](std::size_t i, bool rest_lower) -> int
	{
		const SearchEntry& se = idx_[i];
		if (qv != se.lc8)
			return se.lc8 < qv ? -1 : 1; // сравнение «запись < запрос»
		const char* raw = key_pool_.data() + se.off;
		const std::size_t n_tot = std::min<std::size_t>(se.len, key.size());
		for (std::size_t k = 8; k < n_tot; ++k)
		{
			const char a = lower_byte(raw[k]);
			const char b = qlc[k];
			if (a != b)
				return static_cast<unsigned char>(a) < static_cast<unsigned char>(b) ? -1 : 1;
		}
		if (se.len != key.size())
			return se.len < key.size() ? -1 : 1;
		for (std::size_t k = 0; k < se.len; ++k)
		{
			const auto a = static_cast<unsigned char>(raw[k]);
			const auto b = static_cast<unsigned char>((k == 0 && rest_lower) ? key[k]
																			: qlc[k]);
			if (a != b)
				return a < b ? -1 : 1;
		}
		return 0;
	};

	// FindInStrings: точная последовательность проб оригинала.
	auto find_exact = [&](bool first_raw) -> int
	{
		std::size_t i1 = 0;
		std::size_t i2 = n - 1;
		if (cmp_idx(i1, first_raw) == 0)
			return static_cast<int>(i1);
		if (cmp_idx(i2, first_raw) == 0)
			return static_cast<int>(i2);
		while (i2 - i1 > 1)
		{
			const std::size_t i = (i1 + i2) / 2;
			const int c = cmp_idx(i, first_raw);
			if (c == 0)
				return static_cast<int>(i);
			if (c > 0)
				i2 = i;
			else
				i1 = i;
		}
		return -1;
	};

	const int idx = find_exact(true); // RestLowerCase
	if (idx >= 0)
		return idx;
	const int idx2 = find_exact(false); // полный нижний регистр
	return idx2;
}

int Dictionary::find(UStringView word) const
{
	// Ключ кодируется в переиспользуемый буфер: поиск не аллоцирует.
	thread_local std::string key;
	key.clear();
	cp1251::word_key_into(key, word);
	return find_key(key);
}

UString Dictionary::ye_unicode(std::size_t i) const
{
	return cp1251::decode(entries_[i].ye_raw);
}

UString Dictionary::yo_unicode(std::size_t i) const
{
	return cp1251::decode(entries_[i].yo_raw);
}

void Dictionary::reset_user_sel() noexcept
{
	std::ranges::fill(user_sel_, 0);
}

void Dictionary::note_outcome(std::size_t i, bool old_is_ye, bool old_is_yo, bool new_is_ye,
							  bool new_is_yo) noexcept
{
	auto& s = stats_[i];
	if (old_is_ye)
	{
		if (new_is_ye)
			++s.ye2ye;
		else
			++s.ye2yo;
	}
	if (old_is_yo)
	{
		if (new_is_yo)
			++s.yo2yo;
		else
			++s.yo2ye;
	}
}

} // namespace yo
