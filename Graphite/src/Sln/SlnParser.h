#pragma once

#include "Core/Base/Result.h"
#include "Sln/SolutionModel.h"

#include <filesystem>
#include <string>

namespace Graphite
{
    /// Parses the legacy text solution format. `path` is used to resolve project paths
    /// and to name the solution; `text` is the whole file.
    ///
    /// Separated from reading the file so the parser can be tested on a string, and
    /// because the format is line-oriented rather than a document — there is nothing for
    /// a loader to hand over but the text itself.
    ///
    /// Fills no project contents and does no sorting: ReadSolution does both, after
    /// either parser, so the two formats cannot drift apart.
    [[nodiscard]] Core::Result<Solution> ParseSln(const std::filesystem::path& path,
                                                  const std::string& text);
}
