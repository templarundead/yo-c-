// Переменные окружения без предупреждений MSVC.
//
// std::getenv в MSVC помечен как «небезопасный» (C4996) и с /WX ломает
// сборку. Здесь одно переносимое место: на MSVC используем _dupenv_s, на
// остальных платформах — std::getenv.
#pragma once

#include <cstdlib>
#include <optional>
#include <string>

namespace yo {

// Значение переменной окружения; nullopt, если переменной нет или она пуста.
[[nodiscard]] inline std::optional<std::string> env_var(const char* name)
{
	if (name == nullptr)
		return std::nullopt;
#if defined(_MSC_VER)
	char* buffer = nullptr;
	std::size_t size = 0;
	if (_dupenv_s(&buffer, &size, name) != 0 || buffer == nullptr)
		return std::nullopt;
	std::string value(buffer);
	std::free(buffer);
	if (value.empty())
		return std::nullopt;
	return value;
#else
	const char* value = std::getenv(name);
	if (value == nullptr || *value == '\0')
		return std::nullopt;
	return std::string(value);
#endif
}

} // namespace yo
