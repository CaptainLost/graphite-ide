#include <doctest/doctest.h>

#include "Core/Text/TextBuffer.h"
#include "Editor/DiagnosticOverlay.h"

#include <string>
#include <vector>

namespace {

	Graphite::Diagnostic At(std::size_t line, std::size_t column, std::size_t endColumn,
							std::string message)
	{
		Graphite::Diagnostic diagnostic;
		diagnostic.Line = line;
		diagnostic.Column = Graphite::Utf16Column(column);
		diagnostic.EndLine = line;
		diagnostic.EndColumn = Graphite::Utf16Column(endColumn);
		diagnostic.Severity = Graphite::DiagnosticSeverity::Error;
		diagnostic.Message = std::move(message);

		return diagnostic;
	}
}

TEST_CASE("A span is stored in the coordinates the editor draws in")
{
	// A tab expands to four, so a diagnostic at column 1 of the raw line sits at offset 4 of the
	// text the editor slices. Storing the raw column underlines four characters to the left.
	const Core::TextBuffer buffer{"\tWidget w;\n"};

	Graphite::DiagnosticOverlay overlay;
	overlay.Set({At(0, 1, 7, "no matching constructor")}, buffer, 4);

	const std::vector<Graphite::DiagnosticSpan>& spans = overlay.SpansOn(0);

	REQUIRE(spans.size() == 1);
	CHECK(spans[0].From == 4);
	CHECK(spans[0].To == 10);
	CHECK(spans[0].Message == "no matching constructor");
}

TEST_CASE("A span on a line holding a character outside ASCII lands on the right bytes")
{
	// The protocol counts UTF-16 units and the editor counts bytes of the expanded line. A
	// two-byte character is one unit and two bytes, and only converting through the line's own
	// text gets this right.
	const Core::TextBuffer buffer{"int ą = x;\n"};

	Graphite::DiagnosticOverlay overlay;
	overlay.Set({At(0, 8, 9, "undeclared")}, buffer, 4);

	const std::vector<Graphite::DiagnosticSpan>& spans = overlay.SpansOn(0);

	REQUIRE(spans.size() == 1);
	CHECK(spans[0].From == 9);
	CHECK(spans[0].To == 10);
}

TEST_CASE("A zero-length range is widened to something visible")
{
	// Servers do emit them - a missing semicolon is reported at a point. A span of width zero
	// draws nothing at all, which reads as the diagnostic not existing.
	const Core::TextBuffer buffer{"int x = 1\n"};

	Graphite::DiagnosticOverlay overlay;
	overlay.Set({At(0, 9, 9, "expected ';'")}, buffer, 4);

	const std::vector<Graphite::DiagnosticSpan>& spans = overlay.SpansOn(0);

	REQUIRE(spans.size() == 1);
	CHECK(spans[0].To > spans[0].From);
}

TEST_CASE("A diagnostic spanning several lines is kept on the line it starts on")
{
	// Underlining every line of a multi-line range paints half the file red for one mistake.
	// The panel row still names the true start, which is where the user is sent.
	const Core::TextBuffer buffer{"void f(\n\tint x)\n{\n}\n"};

	Graphite::Diagnostic diagnostic = At(0, 5, 0, "unterminated");
	diagnostic.EndLine = 2;
	diagnostic.EndColumn = Graphite::Utf16Column(1);

	Graphite::DiagnosticOverlay overlay;
	overlay.Set({diagnostic}, buffer, 4);

	CHECK(overlay.SpansOn(0).size() == 1);
	CHECK(overlay.SpansOn(1).empty());
	CHECK(overlay.SpansOn(2).empty());
}

TEST_CASE("A diagnostic past the end of the buffer is dropped rather than drawn")
{
	// A reply can outlive the lines it describes: the buffer shrank between the ask and the
	// answer. SpansOn must not be asked about a line the buffer no longer has.
	const Core::TextBuffer buffer{"int x;\n"};

	Graphite::DiagnosticOverlay overlay;
	overlay.Set({At(40, 0, 3, "gone")}, buffer, 4);

	CHECK(overlay.IsEmpty());
}

TEST_CASE("Set replaces everything")
{
	const Core::TextBuffer buffer{"int x;\n"};

	Graphite::DiagnosticOverlay overlay;
	overlay.Set({At(0, 0, 3, "first")}, buffer, 4);
	overlay.Set({}, buffer, 4);

	CHECK(overlay.IsEmpty());
}
