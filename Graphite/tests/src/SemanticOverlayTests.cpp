#include <doctest/doctest.h>

#include "Core/Text/TextBuffer.h"
#include "Editor/SemanticOverlay.h"
#include "Language/Syntax/Token.h"

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

TEST_CASE("An overlay nothing was said about is empty, and one with a span is not")
{
	const Core::TextBuffer buffer{"int value;\n"};

	Graphite::SemanticOverlay overlay;

	CHECK(overlay.IsEmpty());

	overlay.Set({{0, Graphite::Utf16Column{4}, 5, Graphite::SemanticKind::Member}}, buffer, 4);

	CHECK_FALSE(overlay.IsEmpty());
}

TEST_CASE("An identifier takes the kind of the span that starts where it does")
{
	const Graphite::Token identifier{Graphite::TokenKind::Identifier, 4, 12};
	const std::vector<Graphite::SemanticSpan> spans{
		{4, 12, Graphite::TokenKind::SemanticType}};

	CHECK(Graphite::ColourOf(identifier, spans) == Graphite::TokenKind::SemanticType);
}

TEST_CASE("An identifier no span starts at keeps the colour the lexer gave it")
{
	// A span that merely overlaps is not a match: both sides measure the same tab-expanded
	// text, so two identifiers that begin at different offsets are different identifiers.
	const Graphite::Token identifier{Graphite::TokenKind::Identifier, 4, 12};
	const std::vector<Graphite::SemanticSpan> spans{
		{5, 12, Graphite::TokenKind::SemanticType}};

	CHECK(Graphite::ColourOf(identifier, spans) == Graphite::TokenKind::Identifier);
}

TEST_CASE("A token that is not an identifier is never recoloured")
{
	// The lexer knows a keyword is a keyword. A server names identifiers, so a span reaching
	// one of these is a coordinate error, and taking its colour would spread that error onto
	// the screen instead of ignoring it.
	const Graphite::Token keyword{Graphite::TokenKind::Keyword, 0, 6};
	const std::vector<Graphite::SemanticSpan> spans{
		{0, 6, Graphite::TokenKind::SemanticFunction}};

	CHECK(Graphite::ColourOf(keyword, spans) == Graphite::TokenKind::Keyword);
}

TEST_CASE("An identifier with nothing said about its line keeps the lexer's colour")
{
	const Graphite::Token identifier{Graphite::TokenKind::Identifier, 0, 3};

	CHECK(Graphite::ColourOf(identifier, {}) == Graphite::TokenKind::Identifier);
}

TEST_CASE("An overlay can be walked line by line and built back up again")
{
	Graphite::SemanticOverlay original;
	original.SetSpansOn(3, {Graphite::SemanticSpan{0, 4, Graphite::TokenKind::SemanticType}});
	original.SetSpansOn(7, {Graphite::SemanticSpan{2, 5, Graphite::TokenKind::SemanticType},
							Graphite::SemanticSpan{9, 12, Graphite::TokenKind::SemanticType}});

	Graphite::SemanticOverlay rebuilt;

	original.ForEachLine([&rebuilt](std::size_t line, const std::vector<Graphite::SemanticSpan>& spans) { rebuilt.SetSpansOn(line, spans); });

	CHECK(rebuilt.SpansOn(3).size() == 1);
	CHECK(rebuilt.SpansOn(7).size() == 2);
	CHECK(rebuilt.SpansOn(7)[1].From == 9);
	CHECK(rebuilt.SpansOn(7)[1].To == 12);
	CHECK(rebuilt.SpansOn(0).empty());
	CHECK_FALSE(rebuilt.IsEmpty());
}

TEST_CASE("Setting no spans on a line leaves nothing behind to walk")
{
	Graphite::SemanticOverlay overlay;
	overlay.SetSpansOn(3, {});

	std::size_t visited = 0;

	overlay.ForEachLine([&visited](std::size_t, const std::vector<Graphite::SemanticSpan>&) { ++visited; });

	CHECK(visited == 0);
	CHECK(overlay.IsEmpty());
}
