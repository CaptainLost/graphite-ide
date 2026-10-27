#pragma once

#include "Editor/Authors/AuthorCard.h"
#include "Editor/Authors/LinePeek.h"
#include "Editor/EditorSettings.h"
#include "Editor/Server/LspAnswerRouter.h"
#include "Editor/ScrollAnchor.h"

#include "Core/Command/MenuItem.h"
#include "Core/Text/Editing.h"

#include <chrono>
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
	struct LineAuthors;
	struct LinePeekContent;
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

	// WARNING: Everything but the line count that decides a wrapped row's columns, so a worker wraps as the editor will.
	struct WrapGeometry
	{
		float WidthBesideScrollbar = 0.0f;
		float ScrollbarWidth = 0.0f;
		float FixedGutterWidth = 0.0f;
		float CellWidth = 0.0f;
		float LineNumberGap = 0.0f;

		bool operator==(const WrapGeometry&) const = default;
	};

	enum class VerticalScrollbar
	{
		Shown,
		Hidden
	};

	std::size_t WrappedColumns(const WrapGeometry& geometry, std::size_t lineCount, VerticalScrollbar scrollbar);

	struct LineAuthorView
	{
		const LineAuthors* Authors = nullptr;   // null: draw nothing, offer no card
		std::chrono::sys_seconds Now{};
		const std::chrono::time_zone* Zone = nullptr;   // the reader's, for the card's absolute time; null reads as UTC
		bool HiddenWhileTyping = false;
		float HoverDelaySeconds = 0.5f;
	};

	struct LinePeekView
	{
		std::size_t Line = 0;                       // zero-based buffer line the peek sits under
		const LinePeekContent* Content = nullptr;   // null: the rows are kept and nothing is drawn in them
		std::size_t Selected = 0;
		bool JustOpened = false;                    // scroll so the whole peek shows, if it fits
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

		// Set by a right-click that asks for the context menu: the offset clicked, which inside a selection is not the
		// caret's, because such a click leaves the caret where it was.
		std::optional<std::size_t> ContextMenuOffset;

		std::optional<std::string> CopiedFromMenu;

		std::size_t FirstVisibleRow = 0;
		std::size_t LastVisibleRow = 0;

		bool DrewRows = false;

		std::optional<HoverQuestion> AskHover;
		std::optional<HoverQuestion> AskPreview;
		bool CancelHover = false;

		bool RetryPressed = false;

		std::optional<WrapGeometry> Wrap;

		std::optional<AuthorCardAction> CardAction;

		std::optional<PeekAction> PeekAnswer;
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

		LineAuthorView Author;

		std::optional<LinePeekView> Peek;

		ScrollAnchor* Anchor = nullptr;

		EditorWidgetState* Widget = nullptr;

		HoverState* Hover = nullptr;
		HoverSettings HoverConfig;

		const SemanticOverlay* HoverPreviewOverlay = nullptr;
		PreviewSemantics* HoverSemantics = nullptr;

		std::string HoverLoadingText;
	};

	class FindBar;

	// Only the view holding the keyboard may call it, since it fires the FocusedEditor commands, Close among them.
	// The strokes no command took come back with every typed character, for the view to apply.
	Core::FrameInput RouteFocusedEditorStrokes(const CommandContext& context, const Core::FrameInput& input,
											   bool& ranRepeat);

	EditorFrameResult DrawEditor(const CommandContext& context, Core::OpenItem& item,
								 const EditorView& view, FindBar& findBar);
}
