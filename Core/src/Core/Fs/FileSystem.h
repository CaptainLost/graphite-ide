#pragma once

#include "Core/Base/Result.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace core::fs
{
    /// One entry of a directory listing, as read from disk.
    struct DirectoryEntry
    {
        std::filesystem::path path;
        std::string name;
        bool isDirectory = false;
        bool isHidden = false;
        std::uint64_t size = 0;
    };

    /// Reads a single directory level. std::filesystem throws on failure, so this
    /// wrapper exists to turn that into a Result — Core lets no exception cross a
    /// module boundary.
    Result<std::vector<DirectoryEntry>> ReadDirectory(const std::filesystem::path& path);
}
