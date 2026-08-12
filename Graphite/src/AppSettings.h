#pragma once

#include <cstddef>
#include <filesystem>
#include <string>
#include <string_view>

namespace Core
{
    class Keymap;
}

namespace Graphite
{
    /// Typed access to the values that outlive a session, stored in `graphite.json` next to
    /// `imgui.ini`. Every setter reads the file, changes one key and writes it back, so
    /// settings cannot overwrite each other.
    ///
    /// These are the application's own settings, held once for the program. The ones
    /// belonging to whichever folder is open live in WorkspaceSettings, in that folder's
    /// own .graphite directory, and are rebuilt whenever the root changes.
    class AppSettings
    {
    public:
        /// Returns an empty path when nothing was stored or the file is unreadable.
        static std::filesystem::path LoadLastFolder();
        static void SaveLastFolder(const std::filesystem::path& folder);

        /// Returns an empty string when nothing was stored; the caller decides the
        /// default, since Core has no opinion about display modes.
        static std::string LoadGitIgnoredDisplay();
        static void SaveGitIgnoredDisplay(std::string_view mode);

        /// Returns 0 when nothing was stored, so the caller supplies the default — Core
        /// has no opinion about font sizes.
        static float LoadEditorFontSize();
        static void SaveEditorFontSize(float size);

        /// Returns true when nothing was stored, matching IndentStyle's own default.
        static bool LoadIndentWithSpaces();
        static void SaveIndentWithSpaces(bool useSpaces);

        /// Returns 0 when nothing was stored, so the caller supplies the default — 0 is
        /// not a usable indent width itself.
        static std::size_t LoadIndentWidth();
        static void SaveIndentWidth(std::size_t width);

        /// Returns false when nothing was stored: an editor that starts wrapping without
        /// being asked would be a surprise, and off is what every previous version did.
        static bool LoadWordWrap();
        static void SaveWordWrap(bool wrap);

        /// Applies every stored override to `keys`. Unreadable entries are logged and
        /// skipped: a hand-edited file must not be able to crash the IDE, and one bad
        /// line should cost one binding rather than all of them.
        static void LoadKeyBindings(Core::Keymap& keys);

        /// Writes the overrides and removes stored entries the user has since reset, so
        /// the file holds exactly what differs from the factory table.
        static void SaveKeyBindings(const Core::Keymap& keys);
    };
}
