#pragma once

#include "Core/Text/Columns.h"
#include "Language/Syntax/SyntaxSource.h"
#include "Language/Syntax/Token.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

namespace Core {

	class TextBuffer;
}

namespace Graphite {

	class SyntaxTree;

	inline constexpr std::size_t kLongestTreeColouredRow = 16 * 1024;

	struct ParseBudget
	{
		std::chrono::microseconds PerAttempt{4000};

		std::chrono::microseconds PauseAfterRunningOut{12000};
	};

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

		void SetTabWidth(std::size_t width);

		std::size_t GetTabWidth() const { return m_TabWidth; }

		void Invalidate(std::size_t fromLine);

		void Reset();

		const std::vector<Token>& TokensOf(const Core::TextBuffer& buffer, std::size_t line);

	private:
		void ExtendTo(const Core::TextBuffer& buffer, std::size_t line);
		const std::vector<Token>& LexedTokensOf(const Core::TextBuffer& buffer, std::size_t line);
		const std::vector<Token>& ParsedTokensOf(const Core::TextBuffer& buffer, std::size_t line);
		void BringTreeTowards(const Core::TextBuffer& buffer);
		void CatchUpWith(const Core::TextBuffer& buffer);
		void ForgetEntryStates();
		void ForgetTree();

		SyntaxSource m_Source;
		std::size_t m_TabWidth = Core::DefaultTabWidth;

		std::vector<LineState> m_EntryStates{LineState{}};

		std::vector<Token> m_Tokens;
		std::size_t m_TokensLine = 0;
		bool m_TokensValid = false;

		// The lexer path's place in the buffer's change log. The pointer tells a cache moved to
		// another buffer, whose sequence numbers mean nothing here.
		const Core::TextBuffer* m_SeenBuffer = nullptr;
		std::uint64_t m_SeenChange = 0;

		std::unique_ptr<SyntaxTree> m_Tree;
		std::optional<std::size_t> m_TreeTargetRevision;
		std::optional<std::size_t> m_TreeRevision;
		std::vector<std::optional<std::vector<Token>>> m_RowTokens;
		std::vector<Token> m_ClippedTokens;
		ParseBudget m_ParseBudget;
		std::optional<std::chrono::steady_clock::time_point> m_RanOutAt;
	};
}
