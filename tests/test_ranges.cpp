// Поиск диапазонов, пересекающих ряд: на этом держится отрисовка пометок
// замен и подсветки найденного (список может быть в сотни тысяч записей).

#include "yo/ranges.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <utility>
#include <vector>

using namespace yo;

namespace {

using Range = std::pair<std::size_t, std::size_t>;

std::size_t count(const std::vector<Range>& ranges, std::size_t from, std::size_t to)
{
	const auto [begin, end] = overlapping_ranges(ranges, from, to);
	return end - begin;
}

// Эталон: медленный перебор всех диапазонов.
std::size_t brute(const std::vector<Range>& ranges, std::size_t from, std::size_t to)
{
	std::size_t n = 0;
	for (const Range& r : ranges)
		if (r.first < to && r.second > from)
			++n;
	return n;
}

} // namespace

TEST_CASE("диапазоны: пустой список и пустой отрезок", "[ranges]")
{
	const std::vector<Range> none;
	CHECK(overlapping_ranges(none, 0, 100) == std::pair<std::size_t, std::size_t>{ 0, 0 });

	const std::vector<Range> some = { { 10, 12 } };
	CHECK(overlapping_ranges(some, 20, 20) == std::pair<std::size_t, std::size_t>{ 0, 0 });
	CHECK(overlapping_ranges(some, 30, 10) == std::pair<std::size_t, std::size_t>{ 0, 0 });
}

TEST_CASE("диапазоны: границы полуоткрытых отрезков", "[ranges]")
{
	const std::vector<Range> ranges = { { 0, 2 }, { 5, 7 }, { 10, 13 } };
	// Диапазон [0,2) не пересекает [2,5).
	CHECK(count(ranges, 2, 5) == 0);
	// ...но пересекает [1,2) и [0,3).
	CHECK(count(ranges, 1, 2) == 1);
	CHECK(count(ranges, 0, 3) == 1);
	// [5,7) внутри [5,7) — ровно один.
	CHECK(count(ranges, 5, 7) == 1);
	// Диапазон, начинающийся ровно на конце отрезка, не считается.
	CHECK(count(ranges, 3, 5) == 0);
	CHECK(count(ranges, 7, 10) == 0);
	CHECK(count(ranges, 0, 13) == 3);
	// Всё, что за пределами списка.
	CHECK(count(ranges, 100, 200) == 0);
}

TEST_CASE("диапазоны: пересечение с ряд-окном внутри большого списка", "[ranges]")
{
	std::vector<Range> ranges;
	for (std::size_t i = 0; i < 5000; ++i)
		ranges.emplace_back(i * 10, i * 10 + 2);
	// Ряд «4000..4020» накрывает три диапазона: 4000..4002, 4010..4012,
	// 4020 не входит (начало ровно на конце).
	CHECK(count(ranges, 4000, 4020) == 2);
	const auto [begin, end] = overlapping_ranges(ranges, 4000, 4020);
	REQUIRE(end > begin);
	CHECK(ranges[begin].first == 4000);
	CHECK(ranges[end - 1].first == 4010);
}

TEST_CASE("диапазоны: совпадение с перебором (фаззинг)", "[ranges]")
{
	// Детерминированный генератор: границы, соседние диапазоны, пустые окна.
	std::uint32_t seed = 12345U;
	const auto next = [&seed]() {
		seed = seed * 1103515245U + 12345U;
		return static_cast<std::size_t>((seed >> 16U) & 0xFFU);
	};
	for (int round = 0; round < 40; ++round)
	{
		std::vector<Range> ranges;
		std::size_t pos = 0;
		const std::size_t n = next() % 30;
		for (std::size_t i = 0; i < n; ++i)
		{
			pos += next() % 5;
			const std::size_t len = 1 + next() % 4;
			ranges.emplace_back(pos, pos + len);
			pos += len;
		}
		for (int probe = 0; probe < 40; ++probe)
		{
			const std::size_t from = next() * 4;
			const std::size_t to = from + next();
			CAPTURE(round, probe, from, to);
			CHECK(count(ranges, from, to) == brute(ranges, from, to));
		}
	}
}
