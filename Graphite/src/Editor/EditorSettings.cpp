#include "Editor/EditorSettings.h"

#include "AppSettings.h"

#include "Core/Command/Chord.h"
#include "Core/Command/CommandRegistry.h"
#include "Core/Command/Keymap.h"

#include <algorithm>

namespace graphite
{
    void EditorSettings::Load()
    {
        const float storedFontSize = AppSettings::LoadEditorFontSize();

        if (storedFontSize >= kMinimumFontSize && storedFontSize <= kMaximumFontSize)
        {
            m_fontSize = storedFontSize;
        }

        m_indentWithSpaces = AppSettings::LoadIndentWithSpaces();

        if (const std::size_t storedIndentWidth = AppSettings::LoadIndentWidth();
            storedIndentWidth != 0)
        {
            m_indentWidth = storedIndentWidth;
        }

        m_wordWrap = AppSettings::LoadWordWrap();
    }

    void EditorSettings::SetFontSize(float size)
    {
        const float clamped = std::clamp(size, kMinimumFontSize, kMaximumFontSize);

        if (clamped == m_fontSize)
        {
            return;
        }

        m_fontSize = clamped;
        AppSettings::SaveEditorFontSize(m_fontSize);
    }

    void EditorSettings::SetWordWrap(bool wrap)
    {
        m_wordWrap = wrap;
        AppSettings::SaveWordWrap(m_wordWrap);
    }

    void EditorSettings::RegisterCommands(core::CommandRegistry& commands, core::Keymap& keys)
    {
        commands.Add({.id = "editor.zoomIn",
                      .label = "Zoom In",
                      .run = [this] { SetFontSize(m_fontSize + kFontSizeStep); }});

        commands.Add({.id = "editor.zoomOut",
                      .label = "Zoom Out",
                      .run = [this] { SetFontSize(m_fontSize - kFontSizeStep); }});

        commands.Add({.id = "editor.resetZoom",
                      .label = "Reset Zoom",
                      .run = [this] { SetFontSize(kDefaultFontSize); }});

        // Deliberately no default binding. Ctrl+Z-style obviousness would suggest Alt+Z,
        // and it is a trap here: AltGr on Windows reports as Ctrl+Alt, and AltGr+Z is 'z'
        // with a dot above, so a chord testing Alt alone would toggle wrapping every time
        // a Polish word is typed. Anyone binding this from the shortcuts screen should
        // know the same, which is why it is recorded on the command rather than in the
        // menu it used to sit in.
        commands.Add({.id = "editor.toggleWordWrap",
                      .label = "Word Wrap",
                      .run = [this] { SetWordWrap(!m_wordWrap); },
                      .checked = [this] { return m_wordWrap; }});

        // Both the main row and the keypad, and Ctrl+wheel over an editor. A shortcut
        // nobody can discover is not a shortcut, so all three reach the same command and
        // the menu shows the first of them.
        using core::Chord;
        using core::Scope;
        using core::Trigger;

        keys.SetDefault("editor.zoomIn",
                        {.primary = Chord::Key(ImGuiMod_Ctrl | ImGuiKey_Equal),
                         .alternates = {Chord::Key(ImGuiMod_Ctrl | ImGuiKey_KeypadAdd),
                                        Chord::Wheel(ImGuiMod_Ctrl, Trigger::WheelUp,
                                                     Scope::HoveredEditor)}});

        keys.SetDefault("editor.zoomOut",
                        {.primary = Chord::Key(ImGuiMod_Ctrl | ImGuiKey_Minus),
                         .alternates = {Chord::Key(ImGuiMod_Ctrl | ImGuiKey_KeypadSubtract),
                                        Chord::Wheel(ImGuiMod_Ctrl, Trigger::WheelDown,
                                                     Scope::HoveredEditor)}});

        keys.SetDefault("editor.resetZoom",
                        {.primary = Chord::Key(ImGuiMod_Ctrl | ImGuiKey_0),
                         .alternates = {Chord::Key(ImGuiMod_Ctrl | ImGuiKey_Keypad0)}});
    }
}
