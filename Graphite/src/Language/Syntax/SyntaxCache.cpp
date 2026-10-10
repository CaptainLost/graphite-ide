#include "Language/Syntax/SyntaxCache.h"

#include "Core/App/Redraw.h"
#include "Core/Text/Columns.h"
#include "Core/Text/TextBuffer.h"
#include "Language/Syntax/SyntaxTree.h"

#include <algorithm>
#include <chrono>

namespace Graphite {

	namespace {

		// Function-local rather than a file scope object, per ADR 0033 on complex
		// initialization of globals.
		const std::vector<Token>& NoTokens()
		{
			static const std::vector<Token> tokens;

			return tokens;
		}

		std::vector<Token> ExpandedRowTokens(SyntaxTree& tree, std::size_t row, std::size_t tabWidth)
		{
			std::vector<Token> tokens;
			tree.AppendRowTokens(row, tokens);

			const std::string_view rowText = tree.RowText(row);
			const bool rowHasTab = rowText.find('\t') != std::string_view::npos;

			if (!rowHasTab)
			{
				return tokens;
			}

			for (Token& token : tokens)
			{
				token.From = Core::ExpandedOffsetOfByte(rowText, token.From, tabWidth);
				token.To = Core::ExpandedOffsetOfByte(rowText, token.To, tabWidth);
			}

			return tokens;
		}

		std::vector<Token> ClipToLength(const std::vector<Token>& tokens, std::size_t length)
		{
			std::vector<Token> clipped;

			for (const Token& token : tokens)
			{
				if (token.From >= length)
				{
					break;
				}

				clipped.push_back(Token{.Kind = token.Kind, .From = token.From, .To = std::min(token.To, length)});
			}

			return clipped;
		}
	}

	SyntaxCache::SyntaxCache() = default;
	SyntaxCache::~SyntaxCache() = default;
	SyntaxCache::SyntaxCache(SyntaxCache&&) noexcept = default;
	SyntaxCache& SyntaxCache::operator=(SyntaxCache&&) noexcept = default;

	void SyntaxCache::SetSyntax(SyntaxSource source)
	{
		if (m_Source == source)
		{
			return;
		}

		m_Source = source;
		Reset();
	}

	bool SyntaxCache::HasSyntax() const
	{
		return m_Source.LineLexer != nullptr || m_Source.TreeGrammar != nullptr;
	}

	void SyntaxCache::SetTabWidth(std::size_t width)
	{
		if (width == 0 || width == m_TabWidth)
		{
			return;
		}

		m_TabWidth = width;
		ForgetEntryStates();
		m_RowTokens.assign(m_RowTokens.size(), std::nullopt);
	}

	void SyntaxCache::Invalidate(std::size_t fromLine)
	{
		m_EntryStates.resize(std::min(m_EntryStates.size(), fromLine + 1));
		m_TokensValid = false;
		m_Invalidated = true;
	}

	void SyntaxCache::Reset()
	{
		ForgetEntryStates();
		ForgetTree();
	}

	void SyntaxCache::ForgetEntryStates()
	{
		m_EntryStates.assign(1, LineState{});
		m_TokensValid = false;
		m_HaveRevision = false;
		m_Invalidated = false;
	}

	void SyntaxCache::ForgetTree()
	{
		if (m_Source.TreeGrammar != nullptr)
		{
			m_Tree = std::make_unique<SyntaxTree>(*m_Source.TreeGrammar);
		}
		else
		{
			m_Tree.reset();
		}

		m_TreeTargetRevision.reset();
		m_TreeRevision.reset();
		m_RowTokens.clear();
		m_RanOutAt.reset();
	}

	void SyntaxCache::ExtendTo(const Core::TextBuffer& buffer, std::size_t line)
	{
		std::vector<Token> scratch;

		while (m_EntryStates.size() <= line)
		{
			const std::size_t at = m_EntryStates.size() - 1;

			scratch.clear();
			// Lexed over the tab-expanded text because that is what the editor draws and what
			// its column arithmetic measures. Tabs become spaces, which no token spans.
			const std::string expanded = Core::ExpandTabs(buffer.LineText(at), m_TabWidth);

			m_EntryStates.push_back(m_Source.LineLexer->LexLine(expanded, m_EntryStates[at], scratch));
		}
	}

	const std::vector<Token>& SyntaxCache::TokensOf(const Core::TextBuffer& buffer, std::size_t line)
	{
		if (line >= buffer.GetLineCount())
		{
			return NoTokens();
		}

		if (m_Source.LineLexer != nullptr)
		{
			return LexedTokensOf(buffer, line);
		}

		if (m_Tree != nullptr)
		{
			return ParsedTokensOf(buffer, line);
		}

		return NoTokens();
	}

	const std::vector<Token>& SyntaxCache::LexedTokensOf(const Core::TextBuffer& buffer, std::size_t line)
	{
		const std::size_t revision = buffer.GetRevision();

		if (!m_HaveRevision || m_Revision != revision)
		{
			// A revision that moved with no Invalidate behind it is a mutation path that forgot
			// to call one, and there is no edit location to work from — so drop everything. When
			// Invalidate did run, its targeted resize is the better answer and stands.
			if (m_HaveRevision && !m_Invalidated)
			{
				m_EntryStates.assign(1, LineState{});
				m_TokensValid = false;
			}

			m_Revision = revision;
			m_HaveRevision = true;
			m_Invalidated = false;
		}

		if (m_TokensValid && m_TokensLine == line)
		{
			return m_Tokens;
		}

		ExtendTo(buffer, line);

		m_Tokens.clear();
		const std::string expanded = Core::ExpandTabs(buffer.LineText(line), m_TabWidth);
		m_Source.LineLexer->LexLine(expanded, m_EntryStates[line], m_Tokens);

		m_TokensLine = line;
		m_TokensValid = true;

		return m_Tokens;
	}

	const std::vector<Token>& SyntaxCache::ParsedTokensOf(const Core::TextBuffer& buffer, std::size_t line)
	{
		BringTreeTowards(buffer);

		if (line >= m_RowTokens.size())
		{
			return NoTokens();
		}

		std::optional<std::vector<Token>>& rowTokens = m_RowTokens[line];

		if (!rowTokens.has_value())
		{
			rowTokens = ExpandedRowTokens(*m_Tree, line, m_TabWidth);
		}

		const bool treeLags = m_TreeRevision != buffer.GetRevision();

		if (!treeLags)
		{
			return *rowTokens;
		}

		m_ClippedTokens = ClipToLength(*rowTokens, Core::ExpandTabs(buffer.LineText(line), m_TabWidth).size());
		return m_ClippedTokens;
	}

	void SyntaxCache::BringTreeTowards(const Core::TextBuffer& buffer)
	{
		const std::size_t revision = buffer.GetRevision();

		if (m_TreeRevision == revision)
		{
			return;
		}

		if (buffer.GetLength() > kLargestParsedText)
		{
			ForgetTree();
			m_TreeTargetRevision = revision;
			m_TreeRevision = revision;
			return;
		}

		if (m_TreeTargetRevision != revision)
		{
			m_Tree->Retarget(buffer.Materialize());
			m_TreeTargetRevision = revision;
			m_RanOutAt.reset();
		}

		const std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
		const bool pausing = m_RanOutAt.has_value() && now - *m_RanOutAt < m_ParseBudget.PauseAfterRunningOut;

		if (pausing)
		{
			// A deadline lasts one wait, so a frame landing in the pause has used it up and must
			// ask for the pause's end again.
			Core::Redraw::RequestIn(*m_RanOutAt + m_ParseBudget.PauseAfterRunningOut - now);
			return;
		}

		if (!m_Tree->Advance(m_ParseBudget.PerAttempt))
		{
			m_RanOutAt = std::chrono::steady_clock::now();
			Core::Redraw::RequestIn(m_ParseBudget.PauseAfterRunningOut);
			return;
		}

		m_TreeRevision = revision;
		m_RanOutAt.reset();
		m_RowTokens.assign(m_Tree->GetRowCount(), std::nullopt);
	}
}
