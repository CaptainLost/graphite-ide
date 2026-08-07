#pragma once

#include "Core/Base/Result.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
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

    /// Converts a path to UTF-8. path::string() narrows through the active ANSI
    /// codepage on Windows, which mangles anything outside it — every std::string
    /// in this codebase is UTF-8 by contract, so conversions go through here.
    std::string ToUtf8(const std::filesystem::path& path);

    /// Rebuilds a path from UTF-8 bytes produced by ToUtf8.
    std::filesystem::path FromUtf8(std::string_view text);
}
