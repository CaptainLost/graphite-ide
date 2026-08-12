#pragma once

#include "Core/Sln/ProjectContentReader.h"

namespace Core
{
    /// Reads a Visual C++ project: the file list from the .vcxproj, and the folder
    /// structure from the sibling .vcxproj.filters, which is where Visual Studio keeps it.
    /// Those folders are filters, not directories, and need not match the disk at all.
    class VcxprojReader final : public ProjectContentReader
    {
    public:
        [[nodiscard]] bool Handles(const std::filesystem::path& project) const override;
        [[nodiscard]] Result<ProjectContent> Read(
            const std::filesystem::path& project) const override;
    };
}
