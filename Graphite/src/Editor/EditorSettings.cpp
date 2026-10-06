#include "Editor/EditorSettings.h"

#include "Settings/AppSettings.h"

#include "Core/Command/Chord.h"
#include "Core/Command/CommandRegistry.h"
#include "Core/Command/Keymap.h"

#include <algorithm>
#include <string_view>

namespace Graphite {

	namespace {
		// Stored as words, not numbers, so a hand-edited graphite.json reads as what it means.
		constexpr std::string_view kPreviewOff = "off";
		constexpr std::string_view kPreviewCtrl = "ctrl";
		constexpr std::string_view kPreviewAlways = "always";

		DefinitionPreviewMode PreviewModeFrom(std::string_view stored)
		{
			if (stored == kPreviewOff)
			{
				return DefinitionPreviewMode::Off;
			}

			if (stored == kPreviewAlways)
			{
				return DefinitionPreviewMode::Always;
			}

			// "ctrl", nothing stored, and anything a hand edit got wrong all mean the default.
			return DefinitionPreviewMode::WithCtrl;
		}

		std::string_view PreviewModeName(DefinitionPreviewMode mode)
		{
			switch (mode)
			{
			case DefinitionPreviewMode::Off:
				return kPreviewOff;
			case DefinitionPreviewMode::WithCtrl:
				return kPreviewCtrl;
			case DefinitionPreviewMode::Always:
				return kPreviewAlways;
			}

			return kPreviewCtrl;
		}

		void SetDefaultZoomBindings(Core::Keymap& keys)
		{
			using Core::Chord;
			using Core::ShortcutScope;
			using Core::Trigger;

			keys.SetDefault("editor.zoomIn",
							{.Primary = Chord::Key(ImGuiMod_Ctrl | ImGuiKey_Equal),
							 .Alternates = {Chord::Key(ImGuiMod_Ctrl | ImGuiKey_KeypadAdd),
											Chord::Wheel(ImGuiMod_Ctrl, Trigger::WheelUp,
														 ShortcutScope::HoveredEditor)}});

			keys.SetDefault("editor.zoomOut",
							{.Primary = Chord::Key(ImGuiMod_Ctrl | ImGuiKey_Minus),
							 .Alternates = {Chord::Key(ImGuiMod_Ctrl | ImGuiKey_KeypadSubtract),
											Chord::Wheel(ImGuiMod_Ctrl, Trigger::WheelDown,
														 ShortcutScope::HoveredEditor)}});

			keys.SetDefault("editor.resetZoom",
							{.Primary = Chord::Key(ImGuiMod_Ctrl | ImGuiKey_0),
							 .Alternates = {Chord::Key(ImGuiMod_Ctrl | ImGuiKey_Keypad0)}});
		}
	}

	void EditorSettings::Load()
	{
		const float storedFontSize = m_Store.GetEditorFontSize();

		if (storedFontSize >= kMinimumFontSize && storedFontSize <= kMaximumFontSize)
		{
			m_FontSize = storedFontSize;
		}

		m_IndentWithSpaces = m_Store.IsIndentingWithSpaces();

		// Clamped, not merely accepted: a hand-edited graphite.json is as untrusted as the
		// slider. A malformed width, "-1" included, reads as absent; a well-formed but absurd one
		// such as "999" is what this clamp is for.
		if (const std::size_t storedIndentWidth = m_Store.GetIndentWidth();
			storedIndentWidth != 0)
		{
			m_IndentWidth = std::clamp(storedIndentWidth, kMinimumIndentWidth, kMaximumIndentWidth);
		}

		m_WordWrap = m_Store.IsWordWrapEnabled();
		m_DiffWordWrap = m_Store.IsDiffWordWrapEnabled();
		m_GutterChangeMarkers = m_Store.HasGutterChangeMarkers();
		m_ScrollbarChangeMarkers = m_Store.HasScrollbarChangeMarkers();

		m_Hover.ShowInfo = m_Store.IsHoverInfoEnabled();
		m_Hover.Preview = PreviewModeFrom(m_Store.GetHoverPreview());

		// Zero is "absent" (see NumberOrZero), so it keeps the default rather than clamping to 200.
		if (const int storedDelay = m_Store.GetHoverDelayMs(); storedDelay != 0)
		{
			m_Hover.DelayMs = std::clamp(storedDelay, kMinimumHoverDelayMs, kMaximumHoverDelayMs);
		}
	}

	void EditorSettings::SetFontSize(float size)
	{
		const float clamped = std::clamp(size, kMinimumFontSize, kMaximumFontSize);

		if (clamped == m_FontSize)
		{
			return;
		}

		m_FontSize = clamped;
		m_Store.SetEditorFontSize(m_FontSize);
	}

	void EditorSettings::SetIndentWithSpaces(bool useSpaces)
	{
		m_IndentWithSpaces = useSpaces;
		m_Store.SetIndentWithSpaces(m_IndentWithSpaces);
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
		m_Store.SetIndentWidth(m_IndentWidth);
	}

	void EditorSettings::SetWordWrap(bool wrap)
	{
		m_WordWrap = wrap;
		m_Store.SetWordWrap(m_WordWrap);
	}

	void EditorSettings::SetDiffWordWrap(bool wrap)
	{
		m_DiffWordWrap = wrap;
		m_Store.SetDiffWordWrap(m_DiffWordWrap);
	}

	void EditorSettings::SetGutterChangeMarkers(bool show)
	{
		m_GutterChangeMarkers = show;
		m_Store.SetGutterChangeMarkers(m_GutterChangeMarkers);
	}

	void EditorSettings::SetScrollbarChangeMarkers(bool show)
	{
		m_ScrollbarChangeMarkers = show;
		m_Store.SetScrollbarChangeMarkers(m_ScrollbarChangeMarkers);
	}

	void EditorSettings::SetHoverInfo(bool show)
	{
		m_Hover.ShowInfo = show;
		m_Store.SetHoverInfo(m_Hover.ShowInfo);
	}

	void EditorSettings::SetDefinitionPreview(DefinitionPreviewMode mode)
	{
		m_Hover.Preview = mode;
		m_Store.SetHoverPreview(PreviewModeName(m_Hover.Preview));
	}

	void EditorSettings::SetHoverDelayMs(int ms)
	{
		m_Hover.DelayMs = std::clamp(ms, kMinimumHoverDelayMs, kMaximumHoverDelayMs);
		m_Store.SetHoverDelayMs(m_Hover.DelayMs);
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
