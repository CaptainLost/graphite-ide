#pragma once

#include "Core/Base/Result.h"

#include <filesystem>

namespace core::platform
{
    /// The directory holding the running executable. Use it to resolve files that belong
    /// to the installation — settings, layouts — because the working directory is
    /// whatever the shell that launched the process happened to be in, and the same
    /// install would otherwise read different files depending on how it was started.
    ///
    /// Implemented on Windows only; other platforms return ErrorCode::Unsupported.
    [[nodiscard]] Result<std::filesystem::path> ExecutableDirectory();
}
