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

    /// Whether any window of this process is the one the user is working in.
    ///
    /// Asked of the process rather than of a window on purpose. With ImGui viewports a
    /// panel dragged out of the main window becomes a separate OS window, and asking the
    /// main one whether it has focus then answers "no" while the user is plainly still
    /// using the application.
    ///
    /// Cheap enough to call every frame: one foreground-window lookup and a comparison,
    /// with no cross-process work.
    ///
    /// Where there is no implementation this reports true, so a caller that acts on
    /// *regaining* focus simply never fires rather than firing on every frame. Unlike
    /// the operations above there is no error channel to degrade into, and of the two
    /// possible wrong answers this is the harmless one.
    [[nodiscard]] bool IsForeground();
}
