#pragma once

#include "Core/Base/Result.h"

#include <filesystem>
#include <string_view>

namespace core::fs
{
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
