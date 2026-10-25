#pragma once

#include "Editor/EditorSettings.h"
#include "Editor/Server/LspAnswerRouter.h"
#include "Editor/ScrollAnchor.h"

#include "Core/Command/MenuItem.h"
#include "Core/Text/Editing.h"

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace Core {
	class CommandRegistry;
	class FrameInput;
	class Keymap;
	class ShortcutRouter;
}
namespace Core {
	struct OpenItem;

}

namespace Graphite {
	class LensModel;
	struct EditorWidgetState;
	class SyntaxCache;
	class SemanticOverlay;
	class DiagnosticOverlay;
	struct ChangeMarkers;
	struct HoverState;
	class PreviewSemantics;
	struct ResolvedScheme;

	struct CommandContext
	{
		Core::CommandRegistry* Commands = nullptr;
		Core::Keymap* Keys = nullptr;
		Core::ShortcutRouter* Router = nullptr;

		const Core::FrameInput* Input = nullptr;

		const std::vector<Core::MenuItem>* ContextMenu = nullptr;
	};

	struct ChangeMarkerDisplay
	{
		bool Gutter = true;

		bool Scrollbar = true;
	};

	struct EditorFrameResult
	{
		bool Edited = false;

		bool DefinitionRequested = false;

		float CaretScreenX = 0.0f;
		float CaretScreenY = 0.0f;
		float CaretLineHeight = 0.0f;

		float SurfaceLeft = 0.0f;
		float SurfaceTop = 0.0f;
		float SurfaceRight = 0.0f;
		float SurfaceBottom = 0.0f;

		unsigned int Viewport = 0;

		std::optional<std::size_t> LensClicked;

		bool ContextMenuWanted = false;

		std::optional<std::string> CopiedFromMenu;

		std::size_t FirstVisibleRow = 0;
		std::size_t LastVisibleRow = 0;

		bool DrewRows = false;

		std::optional<HoverQuestion> AskHover;
		std::optional<HoverQuestion> AskPreview;
		bool CancelHover = false;
	};

	struct EditorView
	{
		float FontSize = 0.0f;
		Core::IndentStyle Indent;
		bool WordWrap = false;
		SyntaxCache* Syntax = nullptr;

		SemanticOverlay* Semantic = nullptr;
		const ResolvedScheme* Scheme = nullptr;

		LensModel* Lens = nullptr;

		// WARNING: Hidden rows still follow edits through Lens, so they stand at the right declarations when shown.
		bool ShowLensRows = true;

		std::vector<Declaration>* Declarations = nullptr;

		const DiagnosticOverlay* Diagnostics = nullptr;

		bool DiagnosticsStale = false;

		const ChangeMarkers* Changes = nullptr;

		ChangeMarkerDisplay ChangeDisplay;

		bool HighlightOccurrences = false;

		ScrollAnchor* Anchor = nullptr;

		EditorWidgetState* Widget = nullptr;

		HoverState* Hover = nullptr;
		HoverSettings HoverConfig;

		const SemanticOverlay* HoverPreviewOverlay = nullptr;
		PreviewSemantics* HoverSemantics = nullptr;

		std::string HoverLoadingText;
	};

	class FindBar;

	EditorFrameResult DrawEditor(const CommandContext& context, Core::OpenItem& item,
								 const EditorView& view, FindBar& findBar);
}
