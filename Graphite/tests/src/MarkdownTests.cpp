#include <doctest/doctest.h>

#include "Sessions/Markdown.h"

using namespace Graphite;

TEST_CASE("Spans pick out bold and inline code")
{
	CHECK(ParseSpans("a **b** `c` d") == std::vector<Span>{{SpanStyle::Plain, "a "}, {SpanStyle::Bold, "b"},
														   {SpanStyle::Plain, " "}, {SpanStyle::Code, "c"},
														   {SpanStyle::Plain, " d"}});
}

TEST_CASE("Stars inside inline code stay literal, and an unclosed marker is plain text")
{
	CHECK(ParseSpans("`**x**`") == std::vector<Span>{{SpanStyle::Code, "**x**"}});
	CHECK(ParseSpans("a **b") == std::vector<Span>{{SpanStyle::Plain, "a **b"}});
	CHECK(ParseSpans("a `b") == std::vector<Span>{{SpanStyle::Plain, "a `b"}});
}

TEST_CASE("Headings, list items and paragraphs are told apart")
{
	const std::vector<MarkdownBlock> blocks = ParseMarkdown("## Title\n\nFirst line\ncontinues.\n\n- one\n  - two\n1. three");

	REQUIRE(blocks.size() == 5);
	CHECK(blocks[0].Kind == MarkdownKind::Heading);
	CHECK(blocks[0].Level == 2);
	CHECK(blocks[1].Kind == MarkdownKind::Paragraph);
	CHECK(blocks[1].Spans == std::vector<Span>{{SpanStyle::Plain, "First line continues."}});
	CHECK(blocks[2].Kind == MarkdownKind::ListItem);
	CHECK(blocks[2].Level == 0);
	CHECK(blocks[2].Marker == "\xe2\x80\xa2");
	CHECK(blocks[3].Level == 1);
	CHECK(blocks[4].Marker == "1.");
}

TEST_CASE("A fenced block keeps its text and language verbatim, markers and all")
{
	const std::vector<MarkdownBlock> blocks = ParseMarkdown("```cpp\nint **x** = 0;\n```\nafter");

	REQUIRE(blocks.size() == 2);
	CHECK(blocks[0].Kind == MarkdownKind::Code);
	CHECK(blocks[0].Language == "cpp");
	CHECK(blocks[0].Code == "int **x** = 0;");
	CHECK(blocks[1].Kind == MarkdownKind::Paragraph);
}

TEST_CASE("A fence that never closes runs to the end as code")
{
	const std::vector<MarkdownBlock> blocks = ParseMarkdown("```\nline one\nline two");

	REQUIRE(blocks.size() == 1);
	CHECK(blocks[0].Code == "line one\nline two");
}

TEST_CASE("Odd inputs parse without losing text")
{
	CHECK(ParseMarkdown("").empty());

	const std::vector<MarkdownBlock> fence = ParseMarkdown("```");
	REQUIRE(fence.size() == 1);
	CHECK(fence[0].Kind == MarkdownKind::Code);
	CHECK(fence[0].Code.empty());

	const std::vector<MarkdownBlock> doubleStar = ParseMarkdown("**");
	REQUIRE(doubleStar.size() == 1);
	CHECK(doubleStar[0].Kind == MarkdownKind::Paragraph);
	CHECK(doubleStar[0].Spans == std::vector<Span>{{SpanStyle::Plain, "**"}});

	const std::vector<MarkdownBlock> singleStar = ParseMarkdown("*");
	REQUIRE(singleStar.size() == 1);
	CHECK(singleStar[0].Kind == MarkdownKind::Paragraph);
	CHECK(singleStar[0].Spans == std::vector<Span>{{SpanStyle::Plain, "*"}});

	const std::vector<MarkdownBlock> hashOnly = ParseMarkdown("#");
	REQUIRE(hashOnly.size() == 1);
	CHECK(hashOnly[0].Kind == MarkdownKind::Paragraph);
	CHECK(hashOnly[0].Spans == std::vector<Span>{{SpanStyle::Plain, "#"}});

	const std::vector<MarkdownBlock> crlf = ParseMarkdown("a\r\nb\r");
	REQUIRE(crlf.size() == 1);
	CHECK(crlf[0].Kind == MarkdownKind::Paragraph);
	CHECK(crlf[0].Spans == std::vector<Span>{{SpanStyle::Plain, "a b"}});
}

TEST_CASE("Words wrap at the width, keeping their span")
{
	// Every character is 1 wide.
	const auto measure = [](std::string_view text, SpanStyle) { return static_cast<float>(text.size()); };

	const std::vector<PlacedWord> words =
		LayoutSpans({{SpanStyle::Plain, "aaa bbb "}, {SpanStyle::Bold, "ccc"}}, 8.0f, measure);

	REQUIRE(words.size() == 3);
	CHECK(words[0].Line == 0);
	CHECK(words[1].Line == 0);
	CHECK(words[1].X == doctest::Approx(4.0f));
	CHECK(words[2].Line == 1);
	CHECK(words[2].Span == 1);
	CHECK(words[2].X == doctest::Approx(0.0f));
}

TEST_CASE("A word wider than the whole width gets a line of its own and layout ends")
{
	const auto measure = [](std::string_view text, SpanStyle) { return static_cast<float>(text.size()); };

	const std::vector<PlacedWord> words =
		LayoutSpans({{SpanStyle::Plain, "a D:\\very\\long\\path\\that\\never\\fits b"}}, 6.0f, measure);

	REQUIRE(words.size() == 3);
	CHECK(words[1].Line == 1);
	CHECK(words[2].Line == 2);
}
