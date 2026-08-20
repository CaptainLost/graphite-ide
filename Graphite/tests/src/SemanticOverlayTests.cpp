#include <doctest/doctest.h>

#include "Core/Text/TextBuffer.h"
#include "Editor/SemanticOverlay.h"

#include <string>
#include <vector>

namespace {

	Graphite::SemanticToken Token(std::size_t line, std::size_t column, std::size_t length,
								  Graphite::SemanticKind kind)
	{
		return Graphite::SemanticToken{line, Graphite::Utf16Column(column), length, kind};
	}
}

TEST_CASE("A span is stored in the coordinates the lexer's own tokens use")
{
	// A tab expands to four, so "Renderer" at byte 1 of the raw line sits at offset 4 of the text
	// the lexer was given. Storing the raw byte would put every colour on an indented line four
	// characters to the left of the word it belongs to.
	const Core::TextBuffer buffer{"\tRenderer r;\n"};

	Graphite::SemanticOverlay overlay;
	overlay.Set({Token(0, 1, 8, Graphite::SemanticKind::Type)}, buffer, 4);

	const std::vector<Graphite::SemanticSpan>& spans = overlay.SpansOn(0);

	REQUIRE(spans.size() == 1);
	CHECK(spans[0].From == 4);
	CHECK(spans[0].To == 12);
	CHECK(spans[0].Kind == Graphite::TokenKind::SemanticType);
}

TEST_CASE("A span on a line holding a character outside ASCII keeps its byte width")
{
	// "ą" is two bytes and one UTF-16 unit, and expansion does not change it: the server's column
	// 2 is byte 3, and the expanded text advances by bytes. Two conversions, each in the units its
	// own side counts in - getting either wrong shifts the colour by one character.
	const Core::TextBuffer buffer{"\xC4\x85 Renderer\n"};

	Graphite::SemanticOverlay overlay;
	overlay.Set({Token(0, 2, 8, Graphite::SemanticKind::Type)}, buffer, 4);

	const std::vector<Graphite::SemanticSpan>& spans = overlay.SpansOn(0);

	REQUIRE(spans.size() == 1);
	CHECK(spans[0].From == 3);
	CHECK(spans[0].To == 11);
}

TEST_CASE("Each semantic kind becomes the palette entry drawn for it")
{
	const Core::TextBuffer buffer{"abcdefgh\n"};

	Graphite::SemanticOverlay overlay;
	overlay.Set({Token(0, 0, 1, Graphite::SemanticKind::Type),
				 Token(0, 1, 1, Graphite::SemanticKind::Function),
				 Token(0, 2, 1, Graphite::SemanticKind::Macro),
				 Token(0, 3, 1, Graphite::SemanticKind::Member),
				 Token(0, 4, 1, Graphite::SemanticKind::Parameter),
				 Token(0, 5, 1, Graphite::SemanticKind::Namespace)},
				buffer, 4);

	const std::vector<Graphite::SemanticSpan>& spans = overlay.SpansOn(0);

	REQUIRE(spans.size() == 6);
	CHECK(spans[0].Kind == Graphite::TokenKind::SemanticType);
	CHECK(spans[1].Kind == Graphite::TokenKind::SemanticFunction);
	CHECK(spans[2].Kind == Graphite::TokenKind::SemanticMacro);
	CHECK(spans[3].Kind == Graphite::TokenKind::SemanticMember);
	CHECK(spans[4].Kind == Graphite::TokenKind::SemanticParameter);
	CHECK(spans[5].Kind == Graphite::TokenKind::SemanticNamespace);
}

TEST_CASE("A line nothing was said about has no spans, and neither has one past the end")
{
	const Core::TextBuffer buffer{"a\nb\n"};

	Graphite::SemanticOverlay overlay;
	overlay.Set({Token(1, 0, 1, Graphite::SemanticKind::Type)}, buffer, 4);

	CHECK(overlay.SpansOn(0).empty());
	CHECK(overlay.SpansOn(1).size() == 1);
	CHECK(overlay.SpansOn(9999).empty());
}

TEST_CASE("An edit that adds lines carries the spans below it along")
{
	const Core::TextBuffer buffer{"a\nb\nc\nd\n"};

	Graphite::SemanticOverlay overlay;
	overlay.Set({Token(3, 0, 1, Graphite::SemanticKind::Type)}, buffer, 4);

	overlay.OnLinesChanged(1, 2);

	CHECK(overlay.SpansOn(3).empty());
	CHECK(overlay.SpansOn(5).size() == 1);
}

TEST_CASE("An edit clears the line it happened on, whose columns it just moved")
{
	// The one line whose spans cannot be salvaged: a character typed into it shifts every column
	// after the caret, so keeping them would paint the right colours in the wrong places until
	// the server answers again.
	const Core::TextBuffer buffer{"a\nb\nc\n"};

	Graphite::SemanticOverlay overlay;
	overlay.Set({Token(0, 0, 1, Graphite::SemanticKind::Type),
				 Token(1, 0, 1, Graphite::SemanticKind::Type)},
				buffer, 4);

	overlay.OnLinesChanged(1, 0);

	CHECK(overlay.SpansOn(0).size() == 1);
	CHECK(overlay.SpansOn(1).empty());
}

TEST_CASE("A reply replaces everything an edit had shifted")
{
	const Core::TextBuffer buffer{"a\nb\n"};

	Graphite::SemanticOverlay overlay;
	overlay.Set({Token(0, 0, 1, Graphite::SemanticKind::Type)}, buffer, 4);
	overlay.OnLinesChanged(0, 5);

	overlay.Set({Token(1, 0, 1, Graphite::SemanticKind::Macro)}, buffer, 4);

	CHECK(overlay.SpansOn(0).empty());
	REQUIRE(overlay.SpansOn(1).size() == 1);
	CHECK(overlay.SpansOn(1)[0].Kind == Graphite::TokenKind::SemanticMacro);
}

TEST_CASE("An overlay nothing was said about is empty, and one with a span is not")
{
	const Core::TextBuffer buffer{"int value;\n"};

	Graphite::SemanticOverlay overlay;

	CHECK(overlay.IsEmpty());

	overlay.Set({{0, Graphite::Utf16Column{4}, 5, Graphite::SemanticKind::Member}}, buffer, 4);

	CHECK_FALSE(overlay.IsEmpty());
}
