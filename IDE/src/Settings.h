#pragma once

#include <filesystem>
#include <string>
#include <string_view>

namespace ide
{
    /// Typed access to the values that outlive a session, stored in `ide.json` next to
    /// `imgui.ini`. Every setter reads the file, changes one key and writes it back, so
    /// settings cannot overwrite each other.
    class Settings
    {
    public:
        /// Returns an empty path when nothing was stored or the file is unreadable.
        static std::filesystem::path LoadLastFolder();
        static void SaveLastFolder(const std::filesystem::path& folder);

        /// Returns an empty string when nothing was stored; the caller decides the
        /// default, since Core has no opinion about display modes.
        static std::string LoadIgnoredDisplay();
        static void SaveIgnoredDisplay(std::string_view mode);
    };
}
