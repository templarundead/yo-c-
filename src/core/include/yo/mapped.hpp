// Неизменяемый источник байт: mmap файла или память.
//
// Требование ТЗ: текст открывается без копирования (mmap), правки идут
// поверх в overlay (piece table), исходник не изменяется.
#pragma once

#include <cstddef>
#include <memory>
#include <string>
#include <string_view>

namespace yo {

class ByteSource
{
public:
	ByteSource() = default;
	ByteSource(const ByteSource&) = delete;
	ByteSource& operator=(const ByteSource&) = delete;
	virtual ~ByteSource() = default;

	[[nodiscard]] virtual const char* data() const noexcept = 0;
	[[nodiscard]] virtual std::size_t size() const noexcept = 0;

	[[nodiscard]] std::string_view view() const noexcept { return { data(), size() }; }
};

// Файл, отображённый в память только на чтение.
class MappedFile final : public ByteSource
{
public:
	// nullptr — файла нет/не удалось отобразить (err заполняется).
	static std::shared_ptr<MappedFile> open(const std::string& path, std::string* err = nullptr);
	~MappedFile() override;

	MappedFile(const MappedFile&) = delete;
	MappedFile& operator=(const MappedFile&) = delete;

	[[nodiscard]] const char* data() const noexcept override { return data_; }
	[[nodiscard]] std::size_t size() const noexcept override { return size_; }

private:
	MappedFile() = default;
	void close();

	const char* data_ = nullptr;
	std::size_t size_ = 0;
#ifdef _WIN32
	void* mapping_ = nullptr;	// HANDLE секции (Win32)
#endif
};

// Байты в памяти (результат перекодировки, новый документ, тесты).
class MemorySource final : public ByteSource
{
public:
	explicit MemorySource(std::string bytes) : bytes_(std::move(bytes)) {}

	[[nodiscard]] const char* data() const noexcept override { return bytes_.data(); }
	[[nodiscard]] std::size_t size() const noexcept override { return bytes_.size(); }

private:
	std::string bytes_;
};

} // namespace yo
