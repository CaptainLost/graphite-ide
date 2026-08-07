#pragma once

#include <filesystem>

namespace ide
{
    /// Persists the handful of values that outlive a session, in `ide.ini` next to
    /// `imgui.ini`. Deliberately trivial: a flat `key=value` file, no sections.
    class Settings
    {
    public:
        /// Returns an empty path when nothing was stored or the file is unreadable.
        static std::filesystem::path LoadLastFolder();

        static void SaveLastFolder(const std::filesystem::path& folder);
    };
}
