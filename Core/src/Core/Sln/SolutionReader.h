#pragma once

#include "Core/Base/Result.h"
#include "Core/Sln/SolutionModel.h"

#include <filesystem>

namespace Core
{
    /// Reads a solution file into the model, choosing a parser by extension.
    ///
    /// Both `.slnx` and the legacy `.sln` text format are understood; any other extension
    /// reports ErrorCode::Unsupported.
    ///
    /// Reads the solution only. What is inside each project is a separate layer, so every
    /// Project comes back with contentRead == false.
    /// Whether the extension names a solution format this build can read. Compared
    /// case-insensitively, because the filesystem is.
    ///
    /// Exposed so a caller can refuse a file *before* acting on it. ReadSolution reports
    /// the same thing, but only once it has been called — and a caller that has already
    /// rearranged itself around the choice has nothing useful to do with the answer.
    [[nodiscard]] bool IsSolutionFile(const std::filesystem::path& path);

    [[nodiscard]] Result<Solution> ReadSolution(const std::filesystem::path& path);
}
