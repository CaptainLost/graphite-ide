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

    /// Puts a directory path into the one spelling everything else derives from:
    /// normalised, native separators, and no trailing separator.
    ///
    /// The trailing separator is the part that matters and the part std::filesystem will
    /// not do for you — `lexically_normal` keeps it. `D:/work/` and `D:/work` name the
    /// same directory but do not compare equal, because the first decomposes into an
    /// extra empty element. Anything that later matches a path against this one — a tree
    /// node against a change notification, say — would silently never match. Tab
    /// completion in both cmd and PowerShell appends that separator, so this is a path a
    /// user reaches by accident rather than a malformed one.
    ///
    /// A root such as `D:/` keeps its separator: removing it would name a drive-relative
    /// path, which is a different thing entirely.
    std::filesystem::path NormalizeDirectory(const std::filesystem::path& path);
}
