#pragma once

#include "Core/Sln/ProjectContentReader.h"

namespace Core
{
    /// Reads a C# project. Unlike a .vcxproj, a modern SDK-style .csproj lists no files
    /// at all — it declares an SDK and the SDK globs the project's own directory. So this
    /// reader walks the disk, and its folders are directories rather than the filters a
    /// C++ project carries.
    ///
    /// That inversion is why ProjectContentReader takes a path and returns content rather
    /// than being handed a parsed document: the two implementations share the shape of
    /// the answer and nothing else.
    ///
    /// Only project references come from the file itself.
    class CsprojReader final : public ProjectContentReader
    {
    public:
        [[nodiscard]] bool Handles(const std::filesystem::path& project) const override;
        [[nodiscard]] Result<ProjectContent> Read(
            const std::filesystem::path& project) const override;
    };
}
