#pragma once

#include "Language/Api/LanguageService.h"

#include <cstddef>
#include <map>
#include <string>
#include <vector>

namespace Core {

	class TextBuffer;
}

namespace Graphite {

	struct DiagnosticSpan
	{
		std::size_t From = 0;
		std::size_t To = 0;
		DiagnosticSeverity Severity = DiagnosticSeverity::Error;

		std::string Message;

		std::string Source;
	};

	class DiagnosticOverlay
	{
	public:
		// WARNING: A server counts UTF-16 units of the raw line; the editor slices tab-expanded bytes.
		// Neither conversion can stand in for the other.
		void Set(const std::vector<Diagnostic>& diagnostics, const Core::TextBuffer& buffer,
				 std::size_t tabWidth);

		void Clear();

		bool IsEmpty() const { return m_Lines.empty(); }

		const std::vector<DiagnosticSpan>& SpansOn(std::size_t line) const;

	private:
		std::map<std::size_t, std::vector<DiagnosticSpan>> m_Lines;
	};
}
