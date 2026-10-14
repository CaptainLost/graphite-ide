#include <doctest/doctest.h>

#include "Core/Text/TextBuffer.h"
#include "Editor/Overlays/DiagnosticOverlay.h"

#include <string>
#include <vector>

namespace {

	Graphite::Diagnostic ErrorAt(std::size_t line, std::size_t column, std::size_t endColumn,
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
	const Core::TextBuffer buffer{"\tWidget w;\n"};

	Graphite::DiagnosticOverlay overlay;
	overlay.Set({ErrorAt(0, 1, 7, "no matching constructor")}, buffer, 4);

	const std::vector<Graphite::DiagnosticSpan>& spans = overlay.SpansOn(0);

	REQUIRE(spans.size() == 1);
	CHECK(spans[0].From == 4);
	CHECK(spans[0].To == 10);
	CHECK(spans[0].Message == "no matching constructor");
}

TEST_CASE("A span on a line holding a character outside ASCII lands on the right bytes")
{
	const Core::TextBuffer buffer{"int ą = x;\n"};

	Graphite::DiagnosticOverlay overlay;
	overlay.Set({ErrorAt(0, 8, 9, "undeclared")}, buffer, 4);

	const std::vector<Graphite::DiagnosticSpan>& spans = overlay.SpansOn(0);

	REQUIRE(spans.size() == 1);
	CHECK(spans[0].From == 9);
	CHECK(spans[0].To == 10);
}

TEST_CASE("A zero-length range is widened to something visible")
{
	const Core::TextBuffer buffer{"int x = 1\n"};

	Graphite::DiagnosticOverlay overlay;
	overlay.Set({ErrorAt(0, 9, 9, "expected ';'")}, buffer, 4);

	const std::vector<Graphite::DiagnosticSpan>& spans = overlay.SpansOn(0);

	REQUIRE(spans.size() == 1);
	CHECK(spans[0].To > spans[0].From);
}

TEST_CASE("A diagnostic spanning several lines is kept on the line it starts on")
{
	const Core::TextBuffer buffer{"void f(\n\tint x)\n{\n}\n"};

	Graphite::Diagnostic diagnostic = ErrorAt(0, 5, 0, "unterminated");
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
	const Core::TextBuffer buffer{"int x;\n"};

	Graphite::DiagnosticOverlay overlay;
	overlay.Set({ErrorAt(40, 0, 3, "gone")}, buffer, 4);

	CHECK(overlay.IsEmpty());
}

TEST_CASE("Set replaces everything")
{
	const Core::TextBuffer buffer{"int x;\n"};

	Graphite::DiagnosticOverlay overlay;
	overlay.Set({ErrorAt(0, 0, 3, "first")}, buffer, 4);
	overlay.Set({}, buffer, 4);

	CHECK(overlay.IsEmpty());
}
