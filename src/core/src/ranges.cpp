#include "yo/ranges.hpp"

#include <algorithm>

namespace yo {

std::pair<std::size_t, std::size_t> overlapping_ranges(
	std::span<const std::pair<std::size_t, std::size_t>> ranges, std::size_t from, std::size_t to)
{
	if (ranges.empty() || to <= from)
		return { 0, 0 };
	// Первый диапазон, который заканчивается позже начала отрезка.
	const auto begin = std::lower_bound(ranges.begin(), ranges.end(), from,
										[](const std::pair<std::size_t, std::size_t>& r,
										   std::size_t value) { return r.second <= value; });
	// Первый диапазон, который начинается не раньше конца отрезка.
	const auto end = std::lower_bound(begin, ranges.end(), to,
									  [](const std::pair<std::size_t, std::size_t>& r,
										 std::size_t value) { return r.first < value; });
	return { static_cast<std::size_t>(begin - ranges.begin()),
			 static_cast<std::size_t>(end - ranges.begin()) };
}

} // namespace yo
