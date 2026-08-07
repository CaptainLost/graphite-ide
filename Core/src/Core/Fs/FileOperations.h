#pragma once

#include "Core/Base/Result.h"

#include <filesystem>
#include <string_view>

namespace core::fs
{
    /// Creates an empty file. Fails when anything already occupies the path — silently
    /// overwriting someone's work is never the right default.
    Result<void> CreateEmptyFile(const std::filesystem::path& path);

    /// Creates a single directory. Fails when anything already occupies the path.
    Result<void> MakeDirectory(const std::filesystem::path& path);

    /// Renames or moves an entry. Fails when the source is missing or the target exists.
    Result<void> Rename(const std::filesystem::path& from, const std::filesystem::path& to);

    /// Copies an entry beside itself under a free name and returns that name.
    /// Directories are copied recursively.
    Result<std::filesystem::path> Duplicate(const std::filesystem::path& path);

    /// Moves an entry to the Recycle Bin rather than deleting it, so a mistake stays
    /// recoverable by ordinary system means. Directories go recursively.
    Result<void> MoveToRecycleBin(const std::filesystem::path& path);

    /// Returns `path` unchanged when nothing occupies it, otherwise the first free
    /// variant: "main copy.cpp", then "main copy 2.cpp", and so on. The extension is
    /// whatever std::filesystem calls the extension.
    std::filesystem::path MakeUniquePath(const std::filesystem::path& path);

    /// Rejects names Windows cannot create: empty or blank, containing \ / : * ? " < > |,
    /// ending in a dot or a space, or naming a reserved device (CON, PRN, AUX, NUL,
    /// COM1-COM9, LPT1-LPT9) with or without an extension. Checked before touching the
    /// disk so the user gets a message about the actual problem.
    [[nodiscard]] bool IsValidFileName(std::string_view name);
}
