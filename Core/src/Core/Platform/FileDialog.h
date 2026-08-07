#pragma once

#include "Core/Base/Result.h"

#include <filesystem>
#include <string>

namespace core
{
    class FileDialog
    {
    public:
        /// Opens the system folder picker. Returns a successful Result holding an
        /// empty path when the user cancels — cancelling is not an error, and callers
        /// must check for the empty path before using it.
        static Result<std::filesystem::path> PickFolder(const std::string& title);
    };
}
