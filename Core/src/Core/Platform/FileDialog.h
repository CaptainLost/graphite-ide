#pragma once

#include "Core/Base/Result.h"

#include <filesystem>
#include <string>
#include <vector>

namespace core
{
    class FileDialog
    {
    public:
        /// One entry in the picker's file-type dropdown. `pattern` is the Win32 form —
        /// semicolon-separated, e.g. "*.sln;*.slnx".
        struct Filter
        {
            std::string label;
            std::string pattern;
        };

        /// Opens the system folder picker. Returns a successful Result holding an
        /// empty path when the user cancels — cancelling is not an error, and callers
        /// must check for the empty path before using it.
        ///
        /// Implemented on Windows only. Other platforms return ErrorCode::Unsupported —
        /// which is distinct from the successful empty path that means the user
        /// cancelled.
        static Result<std::filesystem::path> PickFolder(const std::string& title);

        /// Opens the system file picker. Returns a successful Result holding an empty
        /// path when the user cancels — cancelling is not an error, and callers must
        /// check for the empty path before using it.
        ///
        /// An empty filter list means any file.
        ///
        /// Implemented on Windows only. Other platforms return ErrorCode::Unsupported —
        /// which is distinct from the successful empty path that means the user
        /// cancelled.
        [[nodiscard]] static Result<std::filesystem::path> PickFile(
            const std::string& title, const std::vector<Filter>& filters);
    };
}
