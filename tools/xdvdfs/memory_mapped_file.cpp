#include "memory_mapped_file.h"

#if !defined(_WIN32)
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <unistd.h>
#endif

MemoryMappedFile::MemoryMappedFile()
{
}

MemoryMappedFile::MemoryMappedFile(const std::filesystem::path& path)
{
    open(path);
}

MemoryMappedFile::~MemoryMappedFile()
{
    close();
}

MemoryMappedFile::MemoryMappedFile(MemoryMappedFile&& other)
{
#if defined(_WIN32)
    fileHandle = other.fileHandle;
    fileMappingHandle = other.fileMappingHandle;
    fileView = other.fileView;
    fileSize = other.fileSize;

    other.fileHandle = nullptr;
    other.fileMappingHandle = nullptr;
    other.fileView = nullptr;
    other.fileSize.QuadPart = 0;
#else
    fileHandle = other.fileHandle;
    fileView = other.fileView;
    fileSize = other.fileSize;

    other.fileHandle = -1;
    other.fileView = MAP_FAILED;
    other.fileSize = 0;
#endif
}

bool MemoryMappedFile::open(const std::filesystem::path& path)
{
#if defined(_WIN32)
    fileHandle = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (fileHandle == INVALID_HANDLE_VALUE)
    {
        std::fprintf(stderr, "CreateFileW failed with error %lu.\n", GetLastError());
        fileHandle = nullptr;
        return false;
    }

    if (!GetFileSizeEx(fileHandle, &fileSize))
    {
        std::fprintf(stderr, "GetFileSizeEx failed with error %lu.\n", GetLastError());
        CloseHandle(fileHandle);
        fileHandle = nullptr;
        return false;
    }

    fileMappingHandle = CreateFileMappingW(fileHandle, nullptr, PAGE_READONLY, 0, 0, nullptr);
    if (fileMappingHandle == nullptr)
    {
        std::fprintf(stderr, "CreateFileMappingW failed with error %lu.\n", GetLastError());
        CloseHandle(fileHandle);
        fileHandle = nullptr;
        return false;
    }

    fileView = MapViewOfFile(fileMappingHandle, FILE_MAP_READ, 0, 0, 0);
    if (fileView == nullptr)
    {
        std::fprintf(stderr, "MapViewOfFile failed with error %lu.\n", GetLastError());
        CloseHandle(fileMappingHandle);
        CloseHandle(fileHandle);
        fileMappingHandle = nullptr;
        fileHandle = nullptr;
        return false;
    }

    return true;
#else
    fileHandle = ::open(path.c_str(), O_RDONLY);
    if (fileHandle == -1)
    {
        std::fprintf(stderr, "open for %s failed with error %s.\n", path.c_str(), std::strerror(errno));
        return false;
    }

    fileSize = lseek(fileHandle, 0, SEEK_END);
    if (fileSize == static_cast<off_t>(-1))
    {
        std::fprintf(stderr, "lseek failed with error %s.\n", std::strerror(errno));
        ::close(fileHandle);
        fileHandle = -1;
        return false;
    }

    fileView = mmap(nullptr, static_cast<size_t>(fileSize), PROT_READ, MAP_PRIVATE, fileHandle, 0);
    if (fileView == MAP_FAILED)
    {
        std::fprintf(stderr, "mmap failed with error %s.\n", std::strerror(errno));
        ::close(fileHandle);
        fileHandle = -1;
        return false;
    }

    return true;
#endif
}

void MemoryMappedFile::close()
{
#if defined(_WIN32)
    if (fileView != nullptr)
    {
        UnmapViewOfFile(fileView);
    }

    if (fileMappingHandle != nullptr)
    {
        CloseHandle(fileMappingHandle);
    }

    if (fileHandle != nullptr)
    {
        CloseHandle(fileHandle);
    }
#else
    if (fileView != MAP_FAILED)
    {
        munmap(fileView, static_cast<size_t>(fileSize));
    }

    if (fileHandle != -1)
    {
        ::close(fileHandle);
    }
#endif
}

bool MemoryMappedFile::isOpen() const
{
#if defined(_WIN32)
    return fileView != nullptr;
#else
    return fileView != MAP_FAILED;
#endif
}

uint8_t* MemoryMappedFile::data() const
{
    return reinterpret_cast<uint8_t*>(fileView);
}

size_t MemoryMappedFile::size() const
{
#if defined(_WIN32)
    return static_cast<size_t>(fileSize.QuadPart);
#else
    return static_cast<size_t>(fileSize);
#endif
}
