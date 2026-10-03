#include "Editor/Overlays/DiagnosticOverlay.h"

#include "Core/Text/Columns.h"
#include "Core/Text/TextBuffer.h"
#include "Language/Server/LspProtocol.h"

#include <algorithm>

namespace Graphite {

	namespace {

		const std::vector<DiagnosticSpan>& NoSpans()
		{
			// Function-local rather than a namespace-scope object, per ADR 0033 on complex
			// initialization of globals - the same shape SemanticOverlay uses for the same reason.
			static const std::vector<DiagnosticSpan> empty;

			return empty;
		}
	}

	void DiagnosticOverlay::Set(const std::vector<Diagnostic>& diagnostics,
								const Core::TextBuffer& buffer, std::size_t tabWidth)
	{
		m_Lines.clear();

		for (const Diagnostic& diagnostic : diagnostics)
		{
			if (diagnostic.Line >= buffer.GetLineCount())
			{
				// The reply is about a document that has since shrunk. Dropped rather than
				// clamped: a span moved to a line it was never about would underline the wrong
				// text.
				continue;
			}

			const std::string line = buffer.LineText(diagnostic.Line);

			// UTF-16 units to raw bytes, then raw bytes to the expanded offsets the editor slices
			// to draw. Neither step is skippable: the first is wrong on any line holding a
			// character outside ASCII, the second on any line holding a tab.
			const std::size_t fromByte = FromUtf16Column(line, diagnostic.Column);

			// A range spanning several lines is kept only on the line it starts: the end column
			// is meaningless once it names a different line, so it is clamped to this line's own
			// length rather than followed onto one it does not describe.
			const std::size_t toByte = diagnostic.EndLine == diagnostic.Line
										   ? FromUtf16Column(line, diagnostic.EndColumn)
										   : line.size();

			DiagnosticSpan span;
			span.From = Core::ExpandedOffsetOfByte(line, fromByte, tabWidth);
			span.To = Core::ExpandedOffsetOfByte(line, std::max(toByte, fromByte), tabWidth);
			span.Severity = diagnostic.Severity;
			span.Message = diagnostic.Message;
			span.Source = diagnostic.Source;

			if (span.To == span.From)
			{
				// A point diagnostic - a missing semicolon, say. A span of width zero draws
				// nothing at all, which reads as the diagnostic not existing, so it is widened by
				// one rather than dropped.
				++span.To;
			}

			m_Lines[diagnostic.Line].push_back(span);
		}
	}

	void DiagnosticOverlay::Clear()
	{
		m_Lines.clear();
	}

	const std::vector<DiagnosticSpan>& DiagnosticOverlay::SpansOn(std::size_t line) const
	{
		const auto found = m_Lines.find(line);

		return found == m_Lines.end() ? NoSpans() : found->second;
	}
}
