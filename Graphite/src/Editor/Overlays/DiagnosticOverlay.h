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

	// One diagnostic's range on one line, in the coordinates the editor draws in — offsets into
	// the tab-expanded text, because that is what the editor slices when it underlines a span.
	// Converted once, when a reply arrives, so drawing a row converts nothing.
	struct DiagnosticSpan
	{
		std::size_t From = 0;
		std::size_t To = 0;
		DiagnosticSeverity Severity = DiagnosticSeverity::Error;

		// Held by value rather than referenced back into DiagnosticsModel: that model is rebuilt
		// from under this overlay on the next reply, and a pointer into it would dangle in a
		// draw loop that runs long after the update that produced it.
		std::string Message;

		// "clang", "clang-tidy" — shown in the tooltip after the message, same as
		// Diagnostic::Source this is copied from. Empty when the server did not say.
		std::string Source;
	};

	// What a language server said is wrong with one document, kept in the form the editor draws
	// from.
	//
	// A diagnostic overlay and nothing more: it says which span of an already-lexed line to
	// underline and with what, never what that line contains. A document with no server behind
	// it has an empty overlay and is drawn exactly as it was before any of this existed.
	//
	// Two coordinate systems meet here and both conversions are easy to get wrong in ways that
	// look like the underline sliding rather than like arithmetic. A server counts columns in
	// UTF-16 code units of the raw line; the editor counts bytes of the line with its tabs
	// expanded, because that is the text it slices to draw. So a position crosses UTF-16 units to
	// raw bytes, and raw bytes to expanded offsets — the first conversion needs the line's own
	// text, the second needs the tab width, and neither can stand in for the other.
	class DiagnosticOverlay
	{
	public:
		// Replaces everything. A reply is about the whole document, so anything an edit had
		// shifted in the meantime was a stopgap waiting for exactly this.
		void Set(const std::vector<Diagnostic>& diagnostics, const Core::TextBuffer& buffer,
				 std::size_t tabWidth);

		void Clear();

		// True for a document nothing has been said about yet, which is also true of one every
		// diagnostic of which the buffer has since outgrown - the two are the same thing to a
		// reader.
		bool IsEmpty() const { return m_Lines.empty(); }

		// There is deliberately no way to adjust an overlay for an edit. Diagnostics stay where
		// the server put them until a reply replaces them: an underline that has slid a few
		// characters is visibly provisional and costs a reader nothing, while shifting one
		// line-by-line only ever approximated an answer that was already on its way.

		// Empty for a line nothing was said about, which is most of them.
		const std::vector<DiagnosticSpan>& SpansOn(std::size_t line) const;

	private:
		std::map<std::size_t, std::vector<DiagnosticSpan>> m_Lines;
	};
}
