#pragma once

#include "Core/Base/Result.h"
#include "Core/Sln/SolutionModel.h"

#include <filesystem>

namespace core::sln
{
    /// Reads a solution file into the model, choosing a parser by extension.
    ///
    /// Both `.slnx` and the legacy `.sln` text format are understood; any other extension
    /// reports ErrorCode::Unsupported.
    ///
    /// Reads the solution only. What is inside each project is a separate layer, so every
    /// Project comes back with contentRead == false.
    [[nodiscard]] Result<Solution> ReadSolution(const std::filesystem::path& path);
}
