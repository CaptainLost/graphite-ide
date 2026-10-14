#include "Editor/Overlays/SemanticOverlay.h"

#include "Core/Base/EmptyVector.h"
#include "Core/Text/Columns.h"
#include "Core/Text/TextBuffer.h"
#include "Language/Server/LspProtocol.h"

namespace Graphite {

	namespace {

		constexpr std::size_t kBytesPerLine = 64;

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

			// WARNING: Other never reaches here, because ParseSemanticTokens drops it. It is
			// answered with a colour rather than asserted, since a wrong colour is not worth stopping a frame.
			return TokenKind::Text;
		}
	}

	// WARNING: A server counts columns in UTF-16 units of the raw line, while the lexer counts
	// bytes of the tab-expanded line. Both conversions are needed: the first is wrong on any
	// non-ASCII line, the second on any line holding a tab.
	void SemanticOverlay::Set(const std::vector<SemanticToken>& tokens,
							  const Core::TextBuffer& buffer, std::size_t tabWidth)
	{
		m_Lines.clear();

		for (const SemanticToken& token : tokens)
		{
			const bool lineWasRemoved = token.Line >= buffer.GetLineCount();

			if (lineWasRemoved)
			{
				continue;
			}

			const std::string lineText = buffer.LineText(token.Line);

			const std::size_t fromByte = FromUtf16Column(lineText, token.Column);
			const Utf16Column endColumn{token.Column.Value + token.Length};
			const std::size_t toByte = FromUtf16Column(lineText, endColumn);

			SemanticSpan span;
			span.From = Core::ExpandedOffsetOfByte(lineText, fromByte, tabWidth);
			span.To = Core::ExpandedOffsetOfByte(lineText, toByte, tabWidth);
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

	std::size_t SemanticOverlay::Footprint() const
	{
		std::size_t totalBytes = 0;

		for ([[maybe_unused]] const auto& [line, spans] : m_Lines)
		{
			totalBytes += kBytesPerLine + spans.capacity() * sizeof(SemanticSpan);
		}

		return totalBytes;
	}

	const std::vector<SemanticSpan>& SemanticOverlay::SpansOn(std::size_t line) const
	{
		if (!m_Lines.contains(line))
		{
			return Core::SharedEmptyVector<SemanticSpan>();
		}

		return m_Lines.at(line);
	}

	void SemanticOverlay::ForEachLine(
		const std::function<void(std::size_t, const std::vector<SemanticSpan>&)>& visit) const
	{
		for (const auto& [line, spans] : m_Lines)
		{
			visit(line, spans);
		}
	}

	void SemanticOverlay::SetSpansOn(std::size_t line, std::vector<SemanticSpan> spans)
	{
		if (spans.empty())
		{
			return;
		}

		m_Lines[line] = std::move(spans);
	}

	TokenKind ColourOf(const Token& token, const std::vector<SemanticSpan>& semantic)
	{
		if (!NamesSomething(token.Kind))
		{
			return token.Kind;
		}

		for (const SemanticSpan& span : semantic)
		{
			if (span.From == token.From)
			{
				return span.Kind;
			}
		}

		return token.Kind;
	}
}
