#pragma once

#include "Core/Text/Editing.h"

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
	};

	EditorFrameResult DrawEditor(const CommandContext& context, Core::OpenItem& item,
								 const EditorView& view);
}