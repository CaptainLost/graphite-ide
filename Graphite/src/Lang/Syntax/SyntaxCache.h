#pragma once

#include "Core/Text/Columns.h"
#include "Lang/Syntax/Token.h"

#include <cstddef>
#include <vector>

namespace Core {

	class TextBuffer;
}

namespace Graphite {

	// The tokens of one line, and the entry states needed to reach it. A line's colours depend
	// on every line above it — a block comment opened on line 0 reaches line 40 000 — so the
	// entry states are kept and extended rather than recomputed each frame.
	//
	// One cache belongs to one document. It holds no reference to the buffer: the buffer is
	// passed in, so a cache outliving its document cannot dangle.
	class SyntaxCache
	{
	public:
		void SetLexer(const Lexer* lexer);

		bool HasLexer() const { return m_Lexer != nullptr; }

		// A width change moves every token offset in any line holding a tab, because the lexer
		// is fed tab-expanded text. Nothing below the changed width is salvageable, so this
		// resets rather than invalidating from a line.
		void SetTabWidth(std::size_t width);

		// The width the tokens below were produced with. Anything mapping a raw byte of a line
		// onto a token range has to expand tabs by exactly this width, so it is read from here
		// rather than carried alongside the cache where the two could drift apart.
		std::size_t GetTabWidth() const { return m_TabWidth; }

		// Drops the entry states from `fromLine` down. Called after every edit with the line the
		// edit touched: an edit cannot change what a line above it starts in.
		void Invalidate(std::size_t fromLine);

		void Reset();

		// Empty when there is no lexer, and empty for a line past the end of the buffer.
		const std::vector<Token>& TokensOf(const Core::TextBuffer& buffer, std::size_t line);

	private:
		void ExtendTo(const Core::TextBuffer& buffer, std::size_t line);

		const Lexer* m_Lexer = nullptr;
		std::size_t m_TabWidth = Core::DefaultTabWidth;

		// m_EntryStates[n] is the state line n begins in. Always holds at least one element —
		// line 0 begins in the default state — and is valid for lines below its size.
		std::vector<LineState> m_EntryStates{LineState{}};

		std::vector<Token> m_Tokens;
		std::size_t m_TokensLine = 0;
		bool m_TokensValid = false;

		// What the buffer's revision was when the states were computed. A revision that moved
		// without a matching Invalidate means a mutation path forgot to call it, and stale
		// colours would otherwise persist until the file was closed.
		std::size_t m_Revision = 0;
		bool m_HaveRevision = false;

		// Set by Invalidate, cleared when the new revision is adopted. Without it the revision
		// check below would throw away the targeted invalidation it was meant to back up, and
		// every edit would re-lex the file from line 0.
		bool m_Invalidated = false;
	};
}
