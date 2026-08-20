#include "Editor/SemanticOverlay.h"

#include "Core/Text/Columns.h"
#include "Core/Text/TextBuffer.h"
#include "Lang/Server/LspClient.h"

#include <algorithm>
#include <utility>

namespace Graphite {

	namespace {

		const std::vector<SemanticSpan>& NoSpans()
		{
			// Function-local rather than a namespace-scope object, per ADR 0033 on complex
			// initialization of globals - the same shape SyntaxCache uses for the same reason.
			static const std::vector<SemanticSpan> empty;

			return empty;
		}

		TokenKind PaletteEntryFor(SemanticKind kind)
		{
			switch (kind)
			{
				case SemanticKind::Type:
					return TokenKind::SemanticType;
				case SemanticKind::Function:
					return TokenKind::SemanticFunction;
				case SemanticKind::Macro:
					return TokenKind::SemanticMacro;
				case SemanticKind::Member:
					return TokenKind::SemanticMember;
				case SemanticKind::Parameter:
					return TokenKind::SemanticParameter;
				case SemanticKind::Namespace:
					return TokenKind::SemanticNamespace;
				case SemanticKind::Other:
					break;
			}

			// Other never reaches here - ParseSemanticTokens drops it, because a span that changes
			// no colour is one the editor has no use for. Answered anyway rather than asserted:
			// this is a colour, and a wrong one is not worth stopping a frame over.
			return TokenKind::Text;
		}
	}

	void SemanticOverlay::Set(const std::vector<SemanticToken>& tokens,
							  const Core::TextBuffer& buffer, std::size_t tabWidth)
	{
		m_Lines.clear();

		for (const SemanticToken& token : tokens)
		{
			if (token.Line >= buffer.GetLineCount())
			{
				// The reply is about a document that has since shrunk. Dropped rather than
				// clamped: a span moved to a line it was never about would colour the wrong word.
				continue;
			}

			const std::string line = buffer.LineText(token.Line);

			// UTF-16 units to raw bytes, then raw bytes to the expanded offsets the lexer's own
			// tokens are measured in. Neither step is skippable: the first is wrong on any line
			// holding a character outside ASCII, the second on any line holding a tab.
			const std::size_t fromByte = FromUtf16Column(line, token.Column);
			const std::size_t toByte =
				FromUtf16Column(line, Utf16Column{token.Column.Value + token.Length});

			SemanticSpan span;
			span.From = Core::ExpandedOffsetOfByte(line, fromByte, tabWidth);
			span.To = Core::ExpandedOffsetOfByte(line, toByte, tabWidth);
			span.Kind = PaletteEntryFor(token.Kind);

			if (span.To > span.From)
			{
				m_Lines[token.Line].push_back(span);
			}
		}
	}

	void SemanticOverlay::Clear()
	{
		m_Lines.clear();
	}

	void SemanticOverlay::OnLinesChanged(std::size_t fromLine, std::ptrdiff_t delta)
	{
		std::map<std::size_t, std::vector<SemanticSpan>> moved;

		for (auto& [line, spans] : m_Lines)
		{
			if (line < fromLine)
			{
				moved.emplace(line, std::move(spans));
				continue;
			}

			if (line == fromLine)
			{
				// The edited line itself: its columns moved by an amount only the edit knows, so
				// there is nothing here worth carrying forward.
				continue;
			}

			const std::ptrdiff_t shifted = static_cast<std::ptrdiff_t>(line) + delta;

			if (shifted < static_cast<std::ptrdiff_t>(fromLine))
			{
				// A deletion pulled this line above the edit, which means the line it described is
				// gone rather than moved.
				continue;
			}

			moved.emplace(static_cast<std::size_t>(shifted), std::move(spans));
		}

		m_Lines = std::move(moved);
	}

	const std::vector<SemanticSpan>& SemanticOverlay::SpansOn(std::size_t line) const
	{
		const auto found = m_Lines.find(line);

		return found == m_Lines.end() ? NoSpans() : found->second;
	}
}
