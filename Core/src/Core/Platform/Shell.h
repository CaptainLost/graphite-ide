#pragma once

#include "Core/Base/Result.h"

#include <filesystem>

namespace core
{
    class Shell
    {
    public:
        /// Opens the system file browser with the given path selected. Behaves the same
        /// for files and directories: the entry is highlighted inside its parent folder,
        /// rather than the folder being opened, so the user keeps their bearings.
        static Result<void> RevealInFileBrowser(const std::filesystem::path& path);
    };
}
