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

		// The keyboard input left over after the panels and the router have had their turn.
		// The editor reads it while drawing, which is the only moment it knows whether it is
		// the document the input was aimed at.
		const Core::FrameInput* Input = nullptr;

		// The right-click menu's items. Null from a caller that offers no menu; a right-click
		// then only places the caret.
		const std::vector<Core::MenuItem>* ContextMenu = nullptr;
	};

	// Which of the git change markers the reader has switched on. Both on by default, which is
	// also what a document outside any repository is drawn with: it has no markers to show, and
	// the gutter keeps its change column either way.
	struct ChangeMarkerDisplay
	{
		// The gutter's bars and triangles, the tooltip over them, and the column they sit in.
		bool Gutter = true;

		bool Scrollbar = true;
	};

	struct EditorFrameResult
	{
		bool Edited = false;

		// Ctrl+click on the document surface, reported through the result rather than acted on:
		// this widget does not own a LanguageServices, so it can only say what the user asked
		// for, not go ask it. Not restricted to a click that landed on an identifier — F12 does not care
		// what is under the caret either, and this is defined to mean exactly what F12 means from
		// wherever the click just placed it.
		bool DefinitionRequested = false;

		// DrawCaret's own on-screen position, computed every frame regardless of whether
		// anything asked for it: a keyboard-triggered request (F12) never touches the mouse the
		// way Ctrl+click does, so a caller placing a popup "at the caret" for both gestures needs
		// this rather than the pointer position for the keyboard one.
		float CaretScreenX = 0.0f;
		float CaretScreenY = 0.0f;

		// The document row a lens was clicked on, if one was. A lens row holds no text, so the
		// click cannot become a caret position; it is reported instead, for whoever wants to act
		// on the declaration the lens sits above.
		std::optional<std::size_t> LensClicked;

		// A right click on the text, after the caret has been placed for it. The menu is opened
		// by the widget itself; this only says the click qualified.
		bool ContextMenuWanted = false;

		// What a Cut or Copy chosen from a menu put on the clipboard this frame, for the caller
		// to confirm. A Cut or Copy from the chord leaves it empty: that one is not confirmed.
		std::optional<std::string> CopiedFromMenu;

		// The document rows the frame actually drew, so whatever feeds the lenses can ask the
		// server about what is on screen rather than about every declaration in the file. Both
		// zero on a frame that drew nothing, which asks about the top of the document — harmless,
		// and the next frame that draws corrects it.
		std::size_t FirstVisibleRow = 0;
		std::size_t LastVisibleRow = 0;

		// Whether any document row was drawn at all. The two above cannot answer that: they are
		// both zero on a frame that drew nothing AND on a frame that drew only the first row, so a
		// caller comparing them would take the first case for the second and believe the reader
		// was at the top of the file.
		bool DrewRows = false;

		// The hover tracker's asks this frame, with the question each belongs to. Reported rather
		// than sent for the same reason DefinitionRequested is: the widget holds no server.
		std::optional<HoverQuestion> AskHover;
		std::optional<HoverQuestion> AskPreview;
		bool CancelHover = false;
	};

	// What the frame draws with, as opposed to what it draws. Syntax is always supplied today —
	// DocumentWindows::SyntaxFor hands back a valid cache for every document, and one for a
	// language with no lexer simply has TokensOf return an empty vector, so the row still draws
	// in one colour. The null default and the guard around every use are defence, not a
	// supported "no cache" mode: the struct default-constructs to something, and a null pointer
	// is cheaper to guard against than to rule out by construction. Scheme is filled the same
	// way, from the ActiveScheme GraphiteLayer owns and loads at startup.
	struct EditorView
	{
		float FontSize = 0.0f;
		Core::IndentStyle Indent;
		bool WordWrap = false;
		SyntaxCache* Syntax = nullptr;

		// What a language server said this document's identifiers are, drawn over the lexer's own
		// colouring. Null when no server serves this document, which is the ordinary case for a
		// plain text file and for every file before a server has answered - and which the drawing
		// code never has to branch on, because an empty overlay says nothing about any row.
		SemanticOverlay* Semantic = nullptr;
		const ResolvedScheme* Scheme = nullptr;

		// Null when no language server serves this document, which is the ordinary case for a
		// plain text file — the widget then translates through an empty LensRows and draws no
		// lens row, without ever branching on whether a server exists. The same reason an empty
		// token list saves it from branching on Syntax.
		LensModel* Lens = nullptr;

		// Null when the model holds nothing for this path - the same null-means-nothing-to-draw
		// idiom Lens above already uses.
		const DiagnosticOverlay* Diagnostics = nullptr;

		// Read from DiagnosticsModel::IsStale for this document. Passed as a bool rather than by
		// handing the widget the model: the widget draws what it is given and has no business
		// holding something it could query.
		bool DiagnosticsStale = false;

		// How each line differs from git, drawn in the gutter's change column. Null when there
		// is nothing to draw: no repository, an untracked-by-git file, or no answer yet. The
		// column stays reserved either way, unless the reader has switched the gutter's markers
		// off below.
		const ChangeMarkers* Changes = nullptr;

		ChangeMarkerDisplay ChangeDisplay;

		// Where this document's viewport was last time it drew, so a screen-row mapping that
		// changes between frames does not slide the content out from under the reader. Owned by
		// the caller and kept per document, the same way the syntax cache and the overlays above
		// are; the widget reads it at the top of a frame and writes it back at the bottom.
		//
		// Null is a document nothing anchors — a legitimate mode, not a defect, and the widget
		// simply skips both halves. See ScrollAnchor for what the correction is for.
		ScrollAnchor* Anchor = nullptr;

		// What the widget carries from one frame of this document to the next: the caret's blink,
		// a held undo's ramp and the last bracket search. Owned by the caller and kept per
		// document like Anchor above, so two editors on screen never share any of it. Null draws
		// as if every frame were the document's first.
		EditorWidgetState* Widget = nullptr;

		// The document's hover: its tracker, the answers held for it and last frame's tooltip.
		// Null for an editor that asks no server about hovers, which draws a diagnostic under
		// the mouse in a plain tooltip exactly as before hover existed.
		HoverState* Hover = nullptr;
		HoverSettings HoverConfig;

		// What colours the hover's definition preview: the overlay of the tab showing its file,
		// when one does, else what PreviewSemantics hears about it. See HoverTooltipLook.
		const SemanticOverlay* HoverPreviewOverlay = nullptr;
		PreviewSemantics* HoverSemantics = nullptr;
	};

	class FindBar;

	// The find bar is passed in rather than owned here: it remembers which document asked for
	// the keyboard, and that outlives any one frame.
	EditorFrameResult DrawEditor(const CommandContext& context, Core::OpenItem& item,
								 const EditorView& view, FindBar& findBar);
}
