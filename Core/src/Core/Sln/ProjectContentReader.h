#pragma once

#include "Core/Base/Result.h"
#include "Core/Sln/SolutionModel.h"

#include <filesystem>

namespace core::sln
{
    /// Reads what is inside one project file.
    ///
    /// One implementation per project type, chosen by extension. The interface takes a
    /// path and returns content rather than being handed a parsed document, because the
    /// implementations do not share a source of truth: a .vcxproj lists its files, while
    /// a modern .csproj lists none and globs them from its own directory. They agree only
    /// on the shape of the answer.
    class ProjectContentReader
    {
    public:
        virtual ~ProjectContentReader() = default;

        ProjectContentReader(const ProjectContentReader&) = delete;
        ProjectContentReader& operator=(const ProjectContentReader&) = delete;

        [[nodiscard]] virtual bool Handles(const std::filesystem::path& project) const = 0;
        [[nodiscard]] virtual Result<ProjectContent> Read(
            const std::filesystem::path& project) const = 0;

    protected:
        ProjectContentReader() = default;
    };
}
