#pragma once

#include "Core/Text/Editing.h"

#include <cstddef>

namespace Core
{
    class CommandRegistry;
    class Keymap;
}

namespace Graphite
{
    /// The editor's shared settings: one answer for every document, loaded once and
    /// written back when it changes. They live here rather than in the layer because the
    /// commands that change them are registered here too — a setting and the command
    /// that moves it belong together, or the layer ends up owning state only because it
    /// owns the menu that touches it.
    class EditorSettings final
    {
    public:
        /// Reads the stored values, keeping the defaults where nothing was stored or
        /// where a stored value is out of range.
        void Load();

        void RegisterCommands(Core::CommandRegistry& commands, Core::Keymap& keys);

        [[nodiscard]] float FontSize() const noexcept { return m_FontSize; }

        /// Clamps into range, applies and persists — the single path the wheel gesture
        /// and the zoom commands both go through, so they cannot drift apart on what
        /// "zoom in" or "reset" means.
        void SetFontSize(float size);

        [[nodiscard]] Core::IndentStyle IndentStyle() const noexcept
        {
            return {m_IndentWithSpaces, m_IndentWidth};
        }

        [[nodiscard]] bool WordWrap() const noexcept { return m_WordWrap; }

    private:
        void SetWordWrap(bool wrap);

        static constexpr float kMinimumFontSize = 8.0f;
        static constexpr float kMaximumFontSize = 32.0f;
        static constexpr float kDefaultFontSize = 13.0f;

        /// The change applied by one Zoom In or Zoom Out — there is no wheel delta to
        /// read from a menu click, so this stands in for one.
        static constexpr float kFontSizeStep = 1.0f;

        /// Separate from the interface size, so zooming the code leaves the menus alone.
        float m_FontSize = kDefaultFontSize;

        /// What Tab, Enter's auto-indent and the newline splitter insert. No command
        /// edits these yet; they exist so the editor has a real IndentStyle to work with
        /// and so the choice, once something sets it, persists as the font size does.
        bool m_IndentWithSpaces = true;
        std::size_t m_IndentWidth = 4;

        bool m_WordWrap = false;
    };
}
