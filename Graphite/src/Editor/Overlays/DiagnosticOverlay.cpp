#include "Editor/Overlays/DiagnosticOverlay.h"

#include "Core/Base/EmptyVector.h"
#include "Core/Text/Columns.h"
#include "Core/Text/TextBuffer.h"
#include "Language/Server/LspProtocol.h"

#include <algorithm>

namespace Graphite {

	void DiagnosticOverlay::Set(const std::vector<Diagnostic>& diagnostics,
								const Core::TextBuffer& buffer, std::size_t tabWidth)
	{
		m_Lines.clear();

		for (const Diagnostic& diagnostic : diagnostics)
		{
			if (diagnostic.Line >= buffer.GetLineCount())
			{
				continue;
			}

			const std::string lineText = buffer.LineText(diagnostic.Line);

			const std::size_t fromByte = FromUtf16Column(lineText, diagnostic.Column);

			const std::size_t toByte = diagnostic.EndLine == diagnostic.Line
										   ? FromUtf16Column(lineText, diagnostic.EndColumn)
										   : lineText.size();

			DiagnosticSpan span;
			span.From = Core::ExpandedOffsetOfByte(lineText, fromByte, tabWidth);
			span.To = Core::ExpandedOffsetOfByte(lineText, std::max(toByte, fromByte), tabWidth);
			span.Severity = diagnostic.Severity;
			span.Message = diagnostic.Message;
			span.Source = diagnostic.Source;

			if (span.To == span.From)
			{
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
		const std::map<std::size_t, std::vector<DiagnosticSpan>>::const_iterator found = m_Lines.find(line);

		if (found == m_Lines.end())
		{
			return Core::SharedEmptyVector<DiagnosticSpan>();
		}

		const auto& [spanLine, spans] = *found;

		return spans;
	}
}
