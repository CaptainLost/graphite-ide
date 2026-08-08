#pragma once

#include "Core/Base/Result.h"
#include "Core/Sln/SolutionModel.h"

#include <filesystem>

namespace core::sln
{
    /// Reads a solution file into the model, choosing a parser by extension.
    ///
    /// Only `.slnx` is understood today; `.sln` reports ErrorCode::Unsupported until the
    /// legacy parser lands behind this same call.
    ///
    /// Reads the solution only. What is inside each project is a separate layer, so every
    /// Project comes back with contentRead == false.
    [[nodiscard]] Result<Solution> ReadSolution(const std::filesystem::path& path);
}
