#include "Lang/Syntax/SyntaxCache.h"

#include "Core/Text/Columns.h"
#include "Core/Text/TextBuffer.h"

#include <algorithm>

namespace Graphite {

	namespace {

		// Function-local rather than a file scope object, per ADR 0033 on complex
		// initialization of globals.
		const std::vector<Token>& NoTokens()
		{
			static const std::vector<Token> tokens;

			return tokens;
		}
	}

	void SyntaxCache::SetLexer(const Lexer* lexer)
	{
		if (m_Lexer == lexer)
		{
			return;
		}

		m_Lexer = lexer;
		Reset();
	}

	void SyntaxCache::SetTabWidth(std::size_t width)
	{
		if (width == 0 || width == m_TabWidth)
		{
			return;
		}

		m_TabWidth = width;
		Reset();
	}

	void SyntaxCache::Invalidate(std::size_t fromLine)
	{
		m_EntryStates.resize(std::min(m_EntryStates.size(), fromLine + 1));
		m_TokensValid = false;
		m_Invalidated = true;
	}

	void SyntaxCache::Reset()
	{
		m_EntryStates.assign(1, LineState{});
		m_TokensValid = false;
		m_HaveRevision = false;
		m_Invalidated = false;
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

			m_EntryStates.push_back(m_Lexer->LexLine(expanded, m_EntryStates[at], scratch));
		}
	}

	const std::vector<Token>& SyntaxCache::TokensOf(const Core::TextBuffer& buffer,
													std::size_t line)
	{
		if (m_Lexer == nullptr || line >= buffer.GetLineCount())
		{
			return NoTokens();
		}

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
		m_Lexer->LexLine(expanded, m_EntryStates[line], m_Tokens);

		m_TokensLine = line;
		m_TokensValid = true;

		return m_Tokens;
	}
}
