#include "yo/mapped.hpp"

#include <cerrno>
#include <cstring>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace yo {

#ifdef _WIN32

std::shared_ptr<MappedFile> MappedFile::open(const std::string& path, std::string* err)
{
	HANDLE file = CreateFileA(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
							  nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
	if (file == INVALID_HANDLE_VALUE)
	{
		if (err != nullptr)
			*err = "не удалось открыть файл: " + path;
		return nullptr;
	}
	LARGE_INTEGER size{};
	if (!GetFileSizeEx(file, &size) || size.QuadPart == 0)
	{
		CloseHandle(file);
		// Пустой файл: отображать нечего, но это не ошибка.
		return std::shared_ptr<MappedFile>(new MappedFile());
	}
	HANDLE map = CreateFileMappingA(file, nullptr, PAGE_READONLY, 0, 0, nullptr);
	CloseHandle(file);
	if (map == nullptr)
	{
		if (err != nullptr)
			*err = "не удалось отобразить файл: " + path;
		return nullptr;
	}
	const void* view = MapViewOfFile(map, FILE_MAP_READ, 0, 0, 0);
	if (view == nullptr)
	{
		CloseHandle(map);
		if (err != nullptr)
			*err = "не удалось отобразить файл: " + path;
		return nullptr;
	}
	auto mf = std::shared_ptr<MappedFile>(new MappedFile());
	mf->data_ = static_cast<const char*>(view);
	mf->size_ = static_cast<std::size_t>(size.QuadPart);
	mf->mapping_ = map;
	return mf;
}

void MappedFile::close()
{
	if (data_ != nullptr)
		UnmapViewOfFile(data_);
	if (mapping_ != nullptr)
		CloseHandle(static_cast<HANDLE>(mapping_));
	data_ = nullptr;
	size_ = 0;
	mapping_ = nullptr;
}

#else

std::shared_ptr<MappedFile> MappedFile::open(const std::string& path, std::string* err)
{
	const int fd = ::open(path.c_str(), O_RDONLY);
	if (fd < 0)
	{
		if (err != nullptr)
			*err = "не удалось открыть файл: " + path + ": " + std::strerror(errno);
		return nullptr;
	}
	struct stat st{};
	if (::fstat(fd, &st) != 0)
	{
		::close(fd);
		if (err != nullptr)
			*err = "не удалось получить размер файла: " + path;
		return nullptr;
	}
	auto mf = std::shared_ptr<MappedFile>(new MappedFile());
	if (st.st_size == 0)
	{
		::close(fd);
		return mf;
	}
	void* p = ::mmap(nullptr, static_cast<std::size_t>(st.st_size), PROT_READ, MAP_PRIVATE, fd, 0);
	::close(fd);
	if (p == MAP_FAILED)
	{
		if (err != nullptr)
			*err = "не удалось отобразить файл: " + path + ": " + std::strerror(errno);
		return nullptr;
	}
	mf->data_ = static_cast<const char*>(p);
	mf->size_ = static_cast<std::size_t>(st.st_size);
	return mf;
}

void MappedFile::close()
{
	if (data_ != nullptr)
		::munmap(const_cast<char*>(data_), size_);
	data_ = nullptr;
	size_ = 0;
}

#endif

MappedFile::~MappedFile()
{
	close();
}

} // namespace yo
