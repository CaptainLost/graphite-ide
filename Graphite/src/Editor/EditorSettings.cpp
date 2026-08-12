#include "Editor/EditorSettings.h"

#include "Settings/AppSettings.h"

#include "Core/Command/Chord.h"
#include "Core/Command/CommandRegistry.h"
#include "Core/Command/Keymap.h"

#include <algorithm>

namespace Graphite {

	void EditorSettings::Load()
	{
		const float storedFontSize = AppSettings::LoadEditorFontSize();

		if (storedFontSize >= kMinimumFontSize && storedFontSize <= kMaximumFontSize)
		{
			m_FontSize = storedFontSize;
		}

		m_IndentWithSpaces = AppSettings::LoadIndentWithSpaces();

		if (const std::size_t storedIndentWidth = AppSettings::LoadIndentWidth();
			storedIndentWidth != 0)
		{
			m_IndentWidth = storedIndentWidth;
		}

		m_WordWrap = AppSettings::LoadWordWrap();
	}

	void EditorSettings::SetFontSize(float size)
	{
		const float clamped = std::clamp(size, kMinimumFontSize, kMaximumFontSize);

		if (clamped == m_FontSize)
		{
			return;
		}

		m_FontSize = clamped;
		AppSettings::SaveEditorFontSize(m_FontSize);
	}

	void EditorSettings::SetWordWrap(bool wrap)
	{
		m_WordWrap = wrap;
		AppSettings::SaveWordWrap(m_WordWrap);
	}

	void EditorSettings::RegisterCommands(Core::CommandRegistry& commands, Core::Keymap& keys)
	{
		commands.Add({.Id = "editor.zoomIn",
					  .Label = "Zoom In",
					  .Run = [this] { SetFontSize(m_FontSize + kFontSizeStep); }});

		commands.Add({.Id = "editor.zoomOut",
					  .Label = "Zoom Out",
					  .Run = [this] { SetFontSize(m_FontSize - kFontSizeStep); }});

		commands.Add({.Id = "editor.resetZoom",
					  .Label = "Reset Zoom",
					  .Run = [this] { SetFontSize(kDefaultFontSize); }});

		// Deliberately no default binding. Ctrl+Z-style obviousness would suggest Alt+Z,
		// and it is a trap here: AltGr on Windows reports as Ctrl+Alt, and AltGr+Z is 'z'
		// with a dot above, so a chord testing Alt alone would toggle wrapping every time
		// a Polish word is typed. Anyone binding this from the shortcuts screen should
		// know the same, which is why it is recorded on the command rather than in the
		// menu it used to sit in.
		commands.Add({.Id = "editor.toggleWordWrap",
					  .Label = "Word Wrap",
					  .Run = [this] { SetWordWrap(!m_WordWrap); },
					  .Checked = [this] { return m_WordWrap; }});

		// Both the main row and the keypad, and Ctrl+wheel over an editor. A shortcut
		// nobody can discover is not a shortcut, so all three reach the same command and
		// the menu shows the first of them.
		using Core::Chord;
		using Core::Trigger;
		using Scope = Core::Chord::Scope;

		keys.SetDefault("editor.zoomIn",
						{.Primary = Chord::Key(ImGuiMod_Ctrl | ImGuiKey_Equal),
						 .Alternates = {Chord::Key(ImGuiMod_Ctrl | ImGuiKey_KeypadAdd),
										Chord::Wheel(ImGuiMod_Ctrl, Trigger::WheelUp,
													 Chord::Scope::HoveredEditor)}});

		keys.SetDefault("editor.zoomOut",
						{.Primary = Chord::Key(ImGuiMod_Ctrl | ImGuiKey_Minus),
						 .Alternates = {Chord::Key(ImGuiMod_Ctrl | ImGuiKey_KeypadSubtract),
										Chord::Wheel(ImGuiMod_Ctrl, Trigger::WheelDown,
													 Chord::Scope::HoveredEditor)}});

		keys.SetDefault("editor.resetZoom",
						{.Primary = Chord::Key(ImGuiMod_Ctrl | ImGuiKey_0),
						 .Alternates = {Chord::Key(ImGuiMod_Ctrl | ImGuiKey_Keypad0)}});
	}
}