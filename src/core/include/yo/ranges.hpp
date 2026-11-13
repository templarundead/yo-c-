// Поиск диапазонов, пересекающих заданный отрезок.
//
// Списки пометок замен и найденных отсортированы по началу и могут быть
// очень длинными (сотни тысяч записей). Перебирать их целиком на каждый
// видимый ряд нельзя: отрисовка «замирала» на большом файле, а после
// вставки текста выглядела как зависание. Здесь — двоичный поиск, а сам
// алгоритм покрыт тестами без Windows.
#pragma once

#include <cstddef>
#include <span>
#include <utility>

namespace yo {

// Диапазон [begin, end) индексов в `ranges`, пересекающихся с [from, to).
// Диапазоны считаются полуоткрытыми: [first, second). Список должен быть
// отсортирован по возрастанию first; при равенстве first порядок любой.
[[nodiscard]] std::pair<std::size_t, std::size_t> overlapping_ranges(
	std::span<const std::pair<std::size_t, std::size_t>> ranges, std::size_t from, std::size_t to);

} // namespace yo
