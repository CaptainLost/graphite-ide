#pragma once

#include "Core/Base/Result.h"

#include <filesystem>
#include <string_view>

namespace core::fs
{
    /// Writes `bytes` to `path` so that the file is either its old content or its new
    /// content, never half of either.
    ///
    /// The content goes to a temporary beside the target and is then swapped in. Beside,
    /// not in %TEMP%: the same directory means the same volume, and the same inherited
    /// ACLs — a temporary created somewhere with different inheritance can make the swap
    /// fail outright rather than merely lose metadata.
    ///
    /// On Windows an existing target is replaced with ReplaceFileW, which preserves the
    /// target's ACLs, creation time, encryption attribute and file identity; a plain
    /// rename would keep only the content. A target that does not exist has nothing to
    /// replace, so that case moves the temporary into place instead — which is what
    /// saving a file somebody deleted while it was open does.
    ///
    /// This is the one platform split in Core whose #else branch does NOT return
    /// ErrorCode::Unsupported. Everywhere else that answer means graceful degradation;
    /// here it would mean an editor that cannot save. The fallback writes a temporary and
    /// renames it — atomic, without the metadata, which is exactly what every other
    /// editor manages on every platform.
    ///
    /// The stream is closed but not flushed to the platter. That protects against the
    /// process dying, not against loss of power, which is the same trade every editor
    /// examined here makes.
    [[nodiscard]] Result<void> WriteFileAtomically(const std::filesystem::path& path,
                                                    std::string_view bytes);
}
