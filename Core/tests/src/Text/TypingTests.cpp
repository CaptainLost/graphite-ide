#include <doctest/doctest.h>

#include "Core/Text/Caret.h"
#include "Core/Text/DocumentEditing.h"
#include "Core/Text/TextDocument.h"
#include "Core/Text/Typing.h"

#include <optional>
#include <string>
#include <string_view>

using namespace Core;

namespace {
	constexpr IndentStyle kFourSpaces{.UseSpaces = true, .Width = 4};
	constexpr IndentStyle kTabs{.UseSpaces = false, .Width = 4};

	constexpr char kCaretMark = '|';
	constexpr char kAnchorMark = '^';

	// Text with the caret written in as '|' and, for a selection, its other end as '^'.
	struct Marked
	{
		TextDocument Document;
		Caret Caret;
	};

	Marked Parse(std::string_view marked)
	{
		std::string text;
		std::size_t offset = 0;
		std::optional<std::size_t> anchor;

		for (const char character : marked)
		{
			if (character == kCaretMark)
			{
				offset = text.size();
			}
			else if (character == kAnchorMark)
			{
				anchor = text.size();
			}
			else
			{
				text.push_back(character);
			}
		}

		Result<TextDocument> document = TextDocument::FromContent(std::move(text), "test.cpp");
		REQUIRE(document);

		Core::Caret caret;
		caret.Offset = offset;
		caret.Anchor = anchor.value_or(offset);

		return Marked{.Document = std::move(document).GetValue(), .Caret = caret};
	}

	std::string Show(const Marked& marked)
	{
		std::string text = marked.Document.GetBuffer().Materialize();
		const std::size_t offset = marked.Caret.Offset;
		const std::size_t anchor = marked.Caret.Anchor;

		if (anchor == offset)
		{
			return text.insert(offset, 1, kCaretMark);
		}

		if (anchor > offset)
		{
			text.insert(anchor, 1, kAnchorMark);
			return text.insert(offset, 1, kCaretMark);
		}

		text.insert(offset, 1, kCaretMark);
		return text.insert(anchor, 1, kAnchorMark);
	}

	std::string AfterTyping(std::string_view marked, std::string_view typed)
	{
		Marked edited = Parse(marked);

		for (const char character : typed)
		{
			TypeCharacter(edited.Document, edited.Caret, std::string_view(&character, 1));
		}

		return Show(edited);
	}

	std::string AfterEnter(std::string_view marked, IndentStyle style = kFourSpaces)
	{
		Marked edited = Parse(marked);
		REQUIRE(InsertNewline(edited.Document, edited.Caret, style));

		return Show(edited);
	}

	std::string AfterTab(std::string_view marked, IndentStyle style = kFourSpaces)
	{
		Marked edited = Parse(marked);
		REQUIRE(Indent(edited.Document, edited.Caret, style));

		return Show(edited);
	}

	std::string AfterBackspace(std::string_view marked)
	{
		Marked edited = Parse(marked);
		REQUIRE(DeleteBackwardAcrossPair(edited.Document, edited.Caret, kFourSpaces.Width));

		return Show(edited);
	}

	std::string AfterPaste(std::string_view marked, std::string_view pasted, IndentStyle style = kFourSpaces)
	{
		Marked edited = Parse(marked);
		REQUIRE(PasteText(edited.Document, edited.Caret, pasted, style));

		return Show(edited);
	}
}

TEST_CASE("An opening bracket brings its partner and leaves the caret between them")
{
	CHECK(AfterTyping("foo|", "(") == "foo(|)");
	CHECK(AfterTyping("x = |;", "[") == "x = [|];");
	CHECK(AfterTyping("if (x) |", "{") == "if (x) {|}");
	CHECK(AfterTyping("call(|)", "(") == "call((|))");
}

TEST_CASE("An opening bracket typed in front of a word comes alone")
{
	CHECK(AfterTyping("|value", "(") == "(|value");
}

TEST_CASE("A closing bracket typed in front of the same bracket steps over it")
{
	CHECK(AfterTyping("foo|", "()") == "foo()|");
	CHECK(AfterTyping("foo|", "(a)") == "foo(a)|");
	CHECK(AfterTyping("foo|", "();") == "foo();|");
}

TEST_CASE("A bracket or quote typed over a selection goes around it and keeps it selected")
{
	CHECK(AfterTyping("^value|", "(") == "(^value|)");
	CHECK(AfterTyping("|value^", "\"") == "\"|value^\"");
}

TEST_CASE("A quote brings its partner where a string can start and steps over the one that ends it")
{
	CHECK(AfterTyping("say |", "\"") == "say \"|\"");
	CHECK(AfterTyping("say |", "\"\"") == "say \"\"|");
	CHECK(AfterTyping("say |", "\"hi\"") == "say \"hi\"|");
	CHECK(AfterTyping("c = |;", "'") == "c = '|';");
}

TEST_CASE("A quote after a letter or inside a string of its kind comes alone")
{
	CHECK(AfterTyping("don|", "'") == "don'|");
	CHECK(AfterTyping("\"open |", "\"") == "\"open \"|");
	CHECK(AfterTyping("|\"text\"", "\"") == "\"|\"text\"");
}

TEST_CASE("Backspace between a bracket or quote and its partner removes both")
{
	CHECK(AfterBackspace("foo(|)") == "foo|");
	CHECK(AfterBackspace("\"|\"") == "|");
	CHECK(AfterBackspace("foo(|x)") == "foo|x)");
	CHECK(AfterBackspace("ab|") == "a|");
}

TEST_CASE("A closing brace typed on a line of only indentation takes the indentation of the line it closes")
{
	CHECK(AfterTyping("    if (x) {\n        foo();\n        |", "}") == "    if (x) {\n        foo();\n    }|");
	CHECK(AfterTyping("{\n    {\n    }\n        |", "}") == "{\n    {\n    }\n}|");
	CHECK(AfterTyping("void f()\n{\n    |", "}") == "void f()\n{\n}|");
}

TEST_CASE("A closing brace looks past the braces inside strings and line comments")
{
	CHECK(AfterTyping("{\n    s = \"}\"; // }\n        |", "}") == "{\n    s = \"}\"; // }\n}|");
}

TEST_CASE("A closing brace with nothing to close keeps the line as it was")
{
	CHECK(AfterTyping("        |", "}") == "        }|");
}

TEST_CASE("Enter between a bracket and its partner opens an indented line and puts the partner below it")
{
	CHECK(AfterEnter("if (x) {|}") == "if (x) {\n    |\n}");
	CHECK(AfterEnter("    call(|)") == "    call(\n        |\n    )");
	CHECK(AfterEnter("\tif (x) {|}") == "\tif (x) {\n\t\t|\n\t}");
}

TEST_CASE("Enter after an opening bracket indents the new line one level")
{
	CHECK(AfterEnter("    void f() {|") == "    void f() {\n        |");
	CHECK(AfterEnter("void f() {|", kTabs) == "void f() {\n\t|");
	CHECK(AfterEnter("    void f() {  |") == "    void f() {\n        |");
}

TEST_CASE("Enter inside the indentation splits it so the text after the caret stays in its column")
{
	CHECK(AfterEnter("  |  foo") == "  \n  |  foo");
	CHECK(AfterEnter("|    foo") == "\n|    foo");
}

TEST_CASE("Enter leaves no whitespace trailing on the line above or leading the text it moves down")
{
	CHECK(AfterEnter("    a = 1;   |   b") == "    a = 1;\n    |b");
}

TEST_CASE("Tab over a selection inside a line's text replaces it with an indent")
{
	CHECK(AfterTab("a^bc|d") == "a   |d");
	CHECK(AfterTab("    x^ = |1") == "    x   |1");
}

TEST_CASE("Tab over the whole text of a line indents the line")
{
	CHECK(AfterTab("^abc|") == "^    abc|");
	CHECK(AfterTab("    ^abc|") == "^        abc|");
}

TEST_CASE("Tab without a selection pads to the next tab stop")
{
	CHECK(AfterTab("ab|") == "ab  |");
	CHECK(AfterTab("abcd|") == "abcd    |");
	CHECK(AfterTab("ab|", kTabs) == "ab\t|");
}

TEST_CASE("Lines pasted at a line's indentation move as a block to that indentation")
{
	CHECK(AfterPaste("    foo();\n    |", "if (y) {\n    bar();\n}") == "    foo();\n    if (y) {\n        bar();\n    }|");
	CHECK(AfterPaste("    |", "        a();\n            b();\n") == "    a();\n        b();\n|");
}

TEST_CASE("Lines pasted at the start of an indented line go in at its indentation above it")
{
	CHECK(AfterPaste("    {\n|    end();", "  a();\n  b();\n") == "    {\n    a();\n    b();\n|    end();");
}

TEST_CASE("Lines pasted on an empty line take the indentation the lines above give it")
{
	CHECK(AfterPaste("    {\n|", "a();\nb();\n") == "    {\n        a();\n        b();\n|");
	CHECK(AfterPaste("    x();\n|", "a();\n  b();") == "    x();\n    a();\n      b();|");
}

TEST_CASE("Lines pasted after text continue the line and bring the rest to the line's indentation")
{
	CHECK(AfterPaste("    else |", "if (y) {\n        bar();\n    }") == "    else if (y) {\n        bar();\n    }|");
}

TEST_CASE("A paste into a tab-indented line indents with tabs")
{
	CHECK(AfterPaste("\t|", "a\n    b", kFourSpaces) == "\ta\n\t\tb|");
}

TEST_CASE("A paste of one line goes in as it is")
{
	CHECK(AfterPaste("    |", "    x") == "        x|");
	CHECK(AfterPaste("a^bc|d", "X") == "aX|d");
}

TEST_CASE("A paste over a selection replaces it and is one undo step")
{
	Marked edited = Parse("    ^old|");
	REQUIRE(PasteText(edited.Document, edited.Caret, "a();\r\n    b();", kFourSpaces));
	CHECK(Show(edited) == "    a();\n        b();|");

	REQUIRE(edited.Document.GetBuffer().Undo().has_value());
	CHECK(edited.Document.GetBuffer().Materialize() == "    old");
}

TEST_CASE("Reindenting pasted lines empties the blank ones and keeps the deeper ones deeper")
{
	CHECK(ReindentPasted("a\n\n    b\n  \n", "  ", true, kFourSpaces) == "  a\n\n      b\n\n");
	CHECK(ReindentPasted("x {\n        y\n    }", "", false, kFourSpaces) == "x {\n    y\n}");
}
