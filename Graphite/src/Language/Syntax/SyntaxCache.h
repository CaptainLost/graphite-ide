#pragma once

#include "Core/Text/Columns.h"
#include "Language/Syntax/SyntaxSource.h"
#include "Language/Syntax/Token.h"

#include <chrono>
#include <cstddef>
#include <memory>
#include <optional>
#include <vector>

namespace Core {

	class TextBuffer;
}

namespace Graphite {

	class SyntaxTree;

	struct ParseBudget
	{
		std::chrono::microseconds PerAttempt{4000};

		// A frame asks for many lines, and without a pause each of them would spend a whole attempt.
		std::chrono::microseconds PauseAfterRunningOut{12000};
	};

	// The tokens of one line, and the entry states needed to reach it. A line's colours depend
	// on every line above it — a block comment opened on line 0 reaches line 40 000 — so the
	// entry states are kept and extended rather than recomputed each frame.
	//
	// One cache belongs to one document. It holds no reference to the buffer: the buffer is
	// passed in, so a cache outliving its document cannot dangle.
	//
	// A grammar's tree is brought up to date from the buffer's revision; a tree that lags hands
	// out its old tokens, clipped to the line.
	class SyntaxCache
	{
	public:
		SyntaxCache();
		~SyntaxCache();
		SyntaxCache(SyntaxCache&&) noexcept;
		SyntaxCache& operator=(SyntaxCache&&) noexcept;

		void SetSyntax(SyntaxSource source);

		bool HasSyntax() const;

		void SetParseBudget(ParseBudget budget) { m_ParseBudget = budget; }

		// A width change moves every token offset in any line holding a tab, because the lexer
		// is fed tab-expanded text. Nothing below the changed width is salvageable, so this
		// resets rather than invalidating from a line; a grammar's tree is not re-parsed, only
		// its tokens are expanded again.
		void SetTabWidth(std::size_t width);

		// The width the tokens below were produced with. Anything mapping a raw byte of a line
		// onto a token range has to expand tabs by exactly this width, so it is read from here
		// rather than carried alongside the cache where the two could drift apart.
		std::size_t GetTabWidth() const { return m_TabWidth; }

		// Drops the entry states from `fromLine` down. Called after every edit with the line the
		// edit touched: an edit cannot change what a line above it starts in.
		void Invalidate(std::size_t fromLine);

		void Reset();

		// Empty when there is no syntax, and empty for a line past the end of the buffer. Also
		// empty while a grammar's first parse is unfinished, so bracket matching then sees every
		// bracket as code.
		const std::vector<Token>& TokensOf(const Core::TextBuffer& buffer, std::size_t line);

	private:
		void ExtendTo(const Core::TextBuffer& buffer, std::size_t line);
		const std::vector<Token>& LexedTokensOf(const Core::TextBuffer& buffer, std::size_t line);
		const std::vector<Token>& ParsedTokensOf(const Core::TextBuffer& buffer, std::size_t line);
		void BringTreeTowards(const Core::TextBuffer& buffer);
		void ForgetEntryStates();
		void ForgetTree();

		SyntaxSource m_Source;
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

		std::unique_ptr<SyntaxTree> m_Tree;
		std::optional<std::size_t> m_TreeTargetRevision;
		std::optional<std::size_t> m_TreeRevision;
		std::vector<std::optional<std::vector<Token>>> m_RowTokens;
		std::vector<Token> m_ClippedTokens;
		ParseBudget m_ParseBudget;
		std::optional<std::chrono::steady_clock::time_point> m_RanOutAt;
	};
}
