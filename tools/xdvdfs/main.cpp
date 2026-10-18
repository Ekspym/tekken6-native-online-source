#include "memory_mapped_file.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <optional>
#include <stack>
#include <string>
#include <string_view>
#include <tuple>
#include <vector>

namespace {

struct XdvdfsImage {
    MemoryMappedFile mappedFile;
    std::map<std::string, std::tuple<size_t, size_t>> fileMap;
    size_t gameOffset = 0;
    bool valid = false;
};

constexpr size_t kXeSectorSize = 2048;
constexpr size_t kPossibleOffsets[] = {0x00000000, 0x0000FB20, 0x00020600, 0x02080000, 0x0FD90000};
constexpr std::string_view kMagic = "MICROSOFT*XBOX*MEDIA";

bool openImage(const std::filesystem::path& isoPath, XdvdfsImage& outImage)
{
    outImage.mappedFile.open(isoPath);
    if (!outImage.mappedFile.isOpen())
    {
        return false;
    }

    const uint8_t* data = outImage.mappedFile.data();
    bool magicFound = false;

    for (size_t possibleOffset : kPossibleOffsets)
    {
        const size_t fileOffset = possibleOffset + (32 * kXeSectorSize);
        if ((fileOffset + kMagic.size()) > outImage.mappedFile.size())
        {
            continue;
        }

        if (std::memcmp(&data[fileOffset], kMagic.data(), kMagic.size()) == 0)
        {
            outImage.gameOffset = possibleOffset;
            magicFound = true;
        }
    }

    const size_t rootInfoOffset = outImage.gameOffset + (32 * kXeSectorSize) + 20;
    if (!magicFound || (rootInfoOffset + 8) > outImage.mappedFile.size())
    {
        return false;
    }

    const uint32_t rootSector = *reinterpret_cast<const uint32_t*>(&data[rootInfoOffset + 0]);
    const uint32_t rootSize = *reinterpret_cast<const uint32_t*>(&data[rootInfoOffset + 4]);
    const size_t rootOffset = outImage.gameOffset + (static_cast<size_t>(rootSector) * kXeSectorSize);

    if (rootSize < 13 || rootSize > (32 * 1024 * 1024))
    {
        return false;
    }

    struct IterationStep {
        std::string fileNameBase;
        size_t nodeOffset = 0;
        size_t entryOffset = 0;
    };

    std::stack<IterationStep> stack;
    stack.push({"", rootOffset, 0});

    while (!stack.empty())
    {
        const IterationStep step = stack.top();
        stack.pop();

        const size_t infoOffset = step.nodeOffset + step.entryOffset;
        if ((infoOffset + 14) > outImage.mappedFile.size())
        {
            return false;
        }

        const uint16_t nodeL = *reinterpret_cast<const uint16_t*>(&data[infoOffset + 0]);
        const uint16_t nodeR = *reinterpret_cast<const uint16_t*>(&data[infoOffset + 2]);
        const uint32_t sector = *reinterpret_cast<const uint32_t*>(&data[infoOffset + 4]);
        const uint32_t length = *reinterpret_cast<const uint32_t*>(&data[infoOffset + 8]);
        const uint8_t attributes = *reinterpret_cast<const uint8_t*>(&data[infoOffset + 12]);
        const uint8_t nameLength = *reinterpret_cast<const uint8_t*>(&data[infoOffset + 13]);
        const size_t nameOffset = infoOffset + 14;

        if ((nameOffset + nameLength) > outImage.mappedFile.size())
        {
            return false;
        }

        std::string fileName(reinterpret_cast<const char*>(&data[nameOffset]), nameLength);

        if (nodeL != 0)
        {
            stack.push({step.fileNameBase, step.nodeOffset, static_cast<size_t>(nodeL) * 4});
        }

        if (nodeR != 0)
        {
            stack.push({step.fileNameBase, step.nodeOffset, static_cast<size_t>(nodeR) * 4});
        }

        std::string fullName = step.fileNameBase + fileName;
        constexpr uint8_t kDirectoryAttribute = 0x10;
        if ((attributes & kDirectoryAttribute) != 0)
        {
            if (length > 0)
            {
                stack.push({fullName + "/", outImage.gameOffset + static_cast<size_t>(sector) * kXeSectorSize, 0});
            }
        }
        else
        {
            const size_t fileOffset =
                outImage.gameOffset + static_cast<size_t>(sector) * kXeSectorSize;
            if (fileOffset > outImage.mappedFile.size() ||
                length > outImage.mappedFile.size() - fileOffset)
            {
                return false;
            }
            outImage.fileMap[fullName] = {fileOffset, length};
        }
    }

    outImage.valid = true;
    return true;
}

void writeManifest(const XdvdfsImage& image, const std::filesystem::path& outputPath)
{
    std::ofstream out(outputPath);
    if (!out.is_open())
    {
        throw std::runtime_error("failed to open manifest output");
    }

    out << "game_offset=0x" << std::hex << image.gameOffset << std::dec << "\n";
    out << "file_count=" << image.fileMap.size() << "\n";

    for (const auto& [path, entry] : image.fileMap)
    {
        out << std::get<1>(entry) << "\t" << path << "\n";
    }
}

bool extractOne(const XdvdfsImage& image, const std::string& internalPath, const std::filesystem::path& outputPath)
{
    const auto it = image.fileMap.find(internalPath);
    if (it == image.fileMap.end())
    {
        return false;
    }

    const size_t offset = std::get<0>(it->second);
    const size_t length = std::get<1>(it->second);

    std::error_code ec;
    std::filesystem::create_directories(outputPath.parent_path(), ec);

    std::ofstream out(outputPath, std::ios::binary);
    if (!out.is_open())
    {
        throw std::runtime_error("failed to open extraction output");
    }

    out.write(reinterpret_cast<const char*>(image.mappedFile.data() + offset), static_cast<std::streamsize>(length));
    return out.good();
}

std::optional<std::filesystem::path> safeRelativePath(std::string_view internalPath)
{
    std::string normalized(internalPath);
    std::replace(normalized.begin(), normalized.end(), '\\', '/');
    const std::filesystem::path relative = std::filesystem::u8path(normalized);
    if (relative.empty() || relative.is_absolute() || relative.has_root_name() || relative.has_root_directory())
    {
        return std::nullopt;
    }

    for (const auto& component : relative)
    {
        if (component == "." || component == "..")
        {
            return std::nullopt;
        }
    }
    return relative;
}

bool extractAll(const XdvdfsImage& image, const std::filesystem::path& outputRoot)
{
    std::error_code ec;
    if (std::filesystem::exists(outputRoot, ec))
    {
        if (ec || !std::filesystem::is_directory(outputRoot, ec) || ec ||
            !std::filesystem::is_empty(outputRoot, ec) || ec)
        {
            std::cerr << "output directory must not exist or must be empty: " << outputRoot << "\n";
            return false;
        }
    }
    else if (!std::filesystem::create_directories(outputRoot, ec) || ec)
    {
        std::cerr << "failed to create output directory: " << outputRoot << "\n";
        return false;
    }

    size_t extracted = 0;
    for (const auto& [internalPath, entry] : image.fileMap)
    {
        (void)entry;
        const auto relative = safeRelativePath(internalPath);
        if (!relative)
        {
            std::cerr << "unsafe path in image: " << internalPath << "\n";
            return false;
        }
        const auto outputPath = outputRoot / *relative;
        if (!extractOne(image, internalPath, outputPath))
        {
            std::cerr << "failed to extract: " << internalPath << "\n";
            return false;
        }
        ++extracted;
        std::cout << "[" << extracted << "/" << image.fileMap.size() << "] "
                  << internalPath << "\n";
    }
    return true;
}

bool extractRange(
    const XdvdfsImage& image,
    const std::string& internalPath,
    size_t rangeOffset,
    size_t rangeLength,
    const std::filesystem::path& outputPath)
{
    const auto it = image.fileMap.find(internalPath);
    if (it == image.fileMap.end())
    {
        return false;
    }

    const size_t fileOffset = std::get<0>(it->second);
    const size_t fileLength = std::get<1>(it->second);
    if (rangeOffset > fileLength)
    {
        return false;
    }

    const size_t clampedLength = (std::min)(rangeLength, fileLength - rangeOffset);

    std::error_code ec;
    std::filesystem::create_directories(outputPath.parent_path(), ec);

    std::ofstream out(outputPath, std::ios::binary);
    if (!out.is_open())
    {
        throw std::runtime_error("failed to open extraction output");
    }

    out.write(
        reinterpret_cast<const char*>(image.mappedFile.data() + fileOffset + rangeOffset),
        static_cast<std::streamsize>(clampedLength));
    return out.good();
}

void printUsage()
{
    std::cerr
        << "usage:\n"
        << "  xdvdfs_tool list <iso> <manifest>\n"
        << "  xdvdfs_tool extract-all <iso> <empty-output-directory>\n"
        << "  xdvdfs_tool extract <iso> <internal-path> <output>\n"
        << "  xdvdfs_tool extract-range <iso> <internal-path> <offset> <length> <output>\n";
}

} // namespace

int main(int argc, char** argv)
{
    if (argc < 2)
    {
        printUsage();
        return 1;
    }

    const std::string command = argv[1];
    XdvdfsImage image;

    try
    {
        if (command == "extract-all")
        {
            if (argc != 4)
            {
                printUsage();
                return 1;
            }

            if (!openImage(argv[2], image))
            {
                std::cerr << "failed to open XDVDFS image from " << argv[2] << "\n";
                return 2;
            }

            if (!extractAll(image, argv[3]))
            {
                return 3;
            }

            std::cout << "extracted " << image.fileMap.size() << " files to " << argv[3] << "\n";
            return 0;
        }

        if (command == "list")
        {
            if (argc != 4)
            {
                printUsage();
                return 1;
            }

            if (!openImage(argv[2], image))
            {
                std::cerr << "failed to open XDVDFS image from " << argv[2] << "\n";
                return 2;
            }

            writeManifest(image, argv[3]);
            std::cout << "listed " << image.fileMap.size() << " files\n";
            return 0;
        }

        if (command == "extract")
        {
            if (argc != 5)
            {
                printUsage();
                return 1;
            }

            if (!openImage(argv[2], image))
            {
                std::cerr << "failed to open XDVDFS image from " << argv[2] << "\n";
                return 2;
            }

            if (!extractOne(image, argv[3], argv[4]))
            {
                std::cerr << "file not found in image: " << argv[3] << "\n";
                return 3;
            }

            std::cout << "extracted " << argv[3] << "\n";
            return 0;
        }

        if (command == "extract-range")
        {
            if (argc != 7)
            {
                printUsage();
                return 1;
            }

            if (!openImage(argv[2], image))
            {
                std::cerr << "failed to open XDVDFS image from " << argv[2] << "\n";
                return 2;
            }

            const size_t rangeOffset = std::stoull(argv[4], nullptr, 0);
            const size_t rangeLength = std::stoull(argv[5], nullptr, 0);
            if (!extractRange(image, argv[3], rangeOffset, rangeLength, argv[6]))
            {
                std::cerr << "range extraction failed for file: " << argv[3] << "\n";
                return 3;
            }

            std::cout << "extracted range from " << argv[3] << "\n";
            return 0;
        }
    }
    catch (const std::exception& ex)
    {
        std::cerr << ex.what() << "\n";
        return 4;
    }

    printUsage();
    return 1;
}
