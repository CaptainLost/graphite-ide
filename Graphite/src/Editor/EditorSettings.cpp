#include "Editor/EditorSettings.h"

#include "Settings/AppSettings.h"

#include "Core/Command/Chord.h"
#include "Core/Command/CommandRegistry.h"
#include "Core/Command/Keymap.h"

#include <algorithm>

namespace Graphite {

	namespace {
		void SetDefaultZoomBindings(Core::Keymap& keys)
		{
			using Core::Chord;
			using Core::Trigger;

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

	void EditorSettings::Load()
	{
		const float storedFontSize = AppSettings::LoadEditorFontSize();

		if (storedFontSize >= kMinimumFontSize && storedFontSize <= kMaximumFontSize)
		{
			m_FontSize = storedFontSize;
		}

		m_IndentWithSpaces = AppSettings::LoadIndentWithSpaces();

		// Clamped, not merely accepted: a hand-edited graphite.json is as untrusted as the
		// slider, and ParseIndentWidthOrZero's strtoul reads a stored "-1" as ULONG_MAX, so
		// this branch is a real entry point for an unbounded width, not a formality.
		if (const std::size_t storedIndentWidth = AppSettings::LoadIndentWidth();
			storedIndentWidth != 0)
		{
			m_IndentWidth = std::clamp(storedIndentWidth, kMinimumIndentWidth, kMaximumIndentWidth);
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

	void EditorSettings::SetIndentWithSpaces(bool useSpaces)
	{
		m_IndentWithSpaces = useSpaces;
		AppSettings::SaveIndentWithSpaces(m_IndentWithSpaces);
	}

	// Zero is refused rather than clamped: Load treats a stored zero as "use the default", so
	// storing one would make that branch reachable from the UI and leave the width ambiguous.
	//
	// Anything above kMaximumIndentWidth is clamped, not refused: this width is what every
	// visible line containing a tab is expanded to on every frame it is drawn, so an
	// unbounded value here is not a cosmetic oddity but a multi-hundred-megabyte string built
	// per line, per frame, from a single typed number.
	void EditorSettings::SetIndentWidth(std::size_t width)
	{
		if (width == 0)
		{
			return;
		}

		m_IndentWidth = std::clamp(width, kMinimumIndentWidth, kMaximumIndentWidth);
		AppSettings::SaveIndentWidth(m_IndentWidth);
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

		// Deliberately no default binding: AltGr on Windows reports as Ctrl+Alt, so the
		// obvious Alt+Z would fire on every AltGr+Z, which is an ordinary Polish letter.
		commands.Add({.Id = "editor.toggleWordWrap",
					  .Label = "Word Wrap",
					  .Run = [this] { SetWordWrap(!m_WordWrap); },
					  .Checked = [this] { return m_WordWrap; }});

		SetDefaultZoomBindings(keys);
	}
}
