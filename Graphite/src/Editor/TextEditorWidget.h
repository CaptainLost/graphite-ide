#pragma once

#include "Core/Text/Editing.h"

#include <cstddef>
#include <optional>

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
	class SyntaxCache;
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
	};

	struct EditorFrameResult
	{
		bool Edited = false;

		bool SaveRequested = false;

		// Ctrl+click on the document surface, reported the same way SaveRequested is: this
		// widget does not own a LanguageServices, so it can only say what the user asked for, not
		// go ask it. Not restricted to a click that landed on an identifier — F12 does not care
		// what is under the caret either, and this is defined to mean exactly what F12 means from
		// wherever the click just placed it.
		bool DefinitionRequested = false;

		// The document row a lens was clicked on, if one was. A lens row holds no text, so the
		// click cannot become a caret position; it is reported instead, for whoever wants to act
		// on the declaration the lens sits above.
		std::optional<std::size_t> LensClicked;

		// The document rows the frame actually drew, so whatever feeds the lenses can ask the
		// server about what is on screen rather than about every declaration in the file. Both
		// zero on a frame that drew nothing, which asks about the top of the document — harmless,
		// and the next frame that draws corrects it.
		std::size_t FirstVisibleRow = 0;
		std::size_t LastVisibleRow = 0;
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
		const ResolvedScheme* Scheme = nullptr;

		// Null when no language server serves this document, which is the ordinary case for a
		// plain text file — the widget then translates through an empty LensRows and draws no
		// lens row, without ever branching on whether a server exists. The same reason an empty
		// token list saves it from branching on Syntax.
		LensModel* Lens = nullptr;
	};

	class FindBar;

	// The find bar is passed in rather than owned here: it remembers which document asked for
	// the keyboard, and that outlives any one frame.
	EditorFrameResult DrawEditor(const CommandContext& context, Core::OpenItem& item,
								 const EditorView& view, FindBar& findBar);
}
