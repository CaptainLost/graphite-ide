#include "Editor/EditorSettings.h"

#include "Settings/AppSettings.h"

#include "Core/Command/Chord.h"
#include "Core/Command/CommandRegistry.h"
#include "Core/Command/Keymap.h"

#include <algorithm>
#include <string_view>

namespace Graphite {

	namespace {
		constexpr std::string_view kPreviewOff = "off";
		constexpr std::string_view kPreviewCtrl = "ctrl";
		constexpr std::string_view kPreviewAlways = "always";

		constexpr std::string_view kDiffSideBySide = "sideBySide";
		constexpr std::string_view kDiffUnified = "unified";
		constexpr std::string_view kDiffHunks = "hunks";
		constexpr std::string_view kDiffWholeFile = "wholeFile";

		constexpr std::string_view kBackgroundDark = "dark";
		constexpr std::string_view kBackgroundLight = "light";
		constexpr std::string_view kBackgroundCheckerboard = "checkerboard";

		PictureBackground ImageBackgroundFrom(std::string_view stored)
		{
			if (stored == kBackgroundDark)
			{
				return PictureBackground::Dark;
			}

			if (stored == kBackgroundLight)
			{
				return PictureBackground::Light;
			}

			return PictureBackground::Checkerboard;
		}

		std::string_view ImageBackgroundName(PictureBackground background)
		{
			switch (background)
			{
			case PictureBackground::Checkerboard:
				return kBackgroundCheckerboard;
			case PictureBackground::Dark:
				return kBackgroundDark;
			case PictureBackground::Light:
				return kBackgroundLight;
			}

			return kBackgroundCheckerboard;
		}

		DiffViewMode DiffLayoutFrom(std::string_view stored)
		{
			return stored == kDiffUnified ? DiffViewMode::Unified : DiffViewMode::SideBySide;
		}

		std::string_view DiffLayoutName(DiffViewMode mode)
		{
			return mode == DiffViewMode::Unified ? kDiffUnified : kDiffSideBySide;
		}

		DiffScope DiffScopeFrom(std::string_view stored)
		{
			return stored == kDiffWholeFile ? DiffScope::WholeFile : DiffScope::Hunks;
		}

		std::string_view DiffScopeName(DiffScope scope)
		{
			return scope == DiffScope::WholeFile ? kDiffWholeFile : kDiffHunks;
		}

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

		const std::size_t storedIndentWidth = m_Store.GetIndentWidth();
		const bool hasStoredIndentWidth = storedIndentWidth != 0;

		if (hasStoredIndentWidth)
		{
			m_IndentWidth = std::clamp(storedIndentWidth, kMinimumIndentWidth, kMaximumIndentWidth);
		}

		m_WordWrap = m_Store.IsWordWrapEnabled();
		m_DiffView = DiffViewOptions{.Mode = DiffLayoutFrom(m_Store.GetDiffLayout()),
									 .Scope = DiffScopeFrom(m_Store.GetDiffScope()),
									 .ColourSyntax = m_Store.IsDiffSyntaxColoured(),
									 .WordWrap = m_Store.IsDiffWordWrapEnabled()};
		m_ImageBackground = ImageBackgroundFrom(m_Store.GetImageBackground());
		m_GutterChangeMarkers = m_Store.HasGutterChangeMarkers();
		m_ScrollbarChangeMarkers = m_Store.HasScrollbarChangeMarkers();
		m_HighlightOccurrences = m_Store.IsHighlightingOccurrences();

		m_Hover.ShowInfo = m_Store.IsHoverInfoEnabled();
		m_Hover.Preview = PreviewModeFrom(m_Store.GetHoverPreview());

		const int storedDelay = m_Store.GetHoverDelayMs();
		const bool hasStoredDelay = storedDelay != 0;

		if (hasStoredDelay)
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

	// WARNING: Load reads a stored zero as "use the default", so zero is refused here. The
	// maximum is enforced because every visible line with a tab is expanded to this width on
	// every frame, and an unbounded value would build a huge string per line per frame.
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
		m_DiffView.WordWrap = wrap;
		m_Store.SetDiffWordWrap(wrap);
	}

	void EditorSettings::SetDiffViewOptions(const DiffViewOptions& options)
	{
		m_DiffView = options;
		m_Store.SetDiffLayout(DiffLayoutName(options.Mode));
		m_Store.SetDiffScope(DiffScopeName(options.Scope));
		m_Store.SetDiffSyntaxColoured(options.ColourSyntax);
		m_Store.SetDiffWordWrap(options.WordWrap);
	}

	void EditorSettings::SetImageBackground(PictureBackground background)
	{
		m_ImageBackground = background;
		m_Store.SetImageBackground(ImageBackgroundName(background));
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

	void EditorSettings::SetHighlightOccurrences(bool highlight)
	{
		m_HighlightOccurrences = highlight;
		m_Store.SetHighlightOccurrences(m_HighlightOccurrences);
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

	void EditorSettings::SetHoverDelayMs(int milliseconds)
	{
		m_Hover.DelayMs = std::clamp(milliseconds, kMinimumHoverDelayMs, kMaximumHoverDelayMs);
		m_Store.SetHoverDelayMs(m_Hover.DelayMs);
	}

	bool EditorSettings::ZoomsPicture(PictureZoom zoom) const
	{
		return m_PictureZoom && m_PictureZoom(zoom);
	}

	void EditorSettings::RegisterCommands(Core::CommandRegistry& commands, Core::Keymap& keys)
	{
		commands.Add({.Id = "editor.zoomIn",
					  .Label = "Zoom In",
					  .Run = [this] {
						  if (!ZoomsPicture(PictureZoom::In))
						  {
							  SetFontSize(m_FontSize + kFontSizeStep);
						  }
					  }});

		commands.Add({.Id = "editor.zoomOut",
					  .Label = "Zoom Out",
					  .Run = [this] {
						  if (!ZoomsPicture(PictureZoom::Out))
						  {
							  SetFontSize(m_FontSize - kFontSizeStep);
						  }
					  }});

		commands.Add({.Id = "editor.resetZoom",
					  .Label = "Reset Zoom",
					  .Run = [this] {
						  if (!ZoomsPicture(PictureZoom::Fit))
						  {
							  SetFontSize(kDefaultFontSize);
						  }
					  }});

		// WARNING: No default binding on purpose. AltGr on Windows reports as Ctrl+Alt, so Alt+Z
		// would fire on every AltGr+Z, an ordinary Polish letter.
		commands.Add({.Id = "editor.toggleWordWrap",
					  .Label = "Word Wrap",
					  .Run = [this] { SetWordWrap(!m_WordWrap); },
					  .Checked = [this] { return m_WordWrap; }});

		SetDefaultZoomBindings(keys);
	}
}
