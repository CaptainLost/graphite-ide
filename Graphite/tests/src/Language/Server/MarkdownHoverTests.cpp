#include <doctest/doctest.h>

#include "Language/Server/MarkdownHover.h"

#include <ostream>
#include <string>

namespace {

	constexpr const char* kRoslynWriteLine =
		"```csharp\r\n"
		"void System.Console.WriteLine(string? value) (+ 19 przeci\xC4\x85\xC5\xBC" "enia)\r\n"
		"```\r\n"
		"  \r\n"
		"Writes the specified string value, followed by the current line terminator, to the standard output stream\\.  \r\n"
		"  \r\n"
		"Wyj\xC4\x85" "tki:  \r\n"
		"&nbsp;&nbsp;System\\.IO\\.IOException  \r\n";

	constexpr const char* kOmniSharpWriteLine =
		"```csharp\n"
		"void System.Console.WriteLine(string? value) (+ 19 przeci\xC4\x85\xC5\xBC" "enia)\n"
		"```\n"
		"\n"
		"Writes the specified string value, followed by the current line terminator, to the standard output stream\\.\n"
		"\n"
		"Wyj\xC4\x85" "tki:\n"
		"\n"
		"```csharp\n"
		"  System.IO.IOException\n"
		"```";

	constexpr const char* kCsharpLsWriteLine =
		"```csharp\n"
		"void Console.WriteLine(string? value)\n"
		"```\n"
		"\n"
		"Writes the specified string value, followed by the current line terminator, to the standard output stream.\n"
		"\n"
		"Parameters:\n"
		"- ``value``: The value to write.\n"
		"\n"
		"Exceptions:\n"
		"- ``System.IO.IOException``: An I/O error occurred.";

	constexpr const char* kWriteLineSummary =
		"Writes the specified string value, followed by the current line terminator, to the standard output stream.";
}

TEST_CASE("Roslyn's hover reads as its declaration and its overload note and its documentation without escapes")
{
	const Graphite::HoverInfo info = Graphite::ParseMarkdownHover(kRoslynWriteLine);

	CHECK(info.Parsed);
	CHECK(info.Declaration == "void System.Console.WriteLine(string? value)");
	CHECK(info.OverloadNote == std::string{"(+ 19 przeci\xC4\x85\xC5\xBC" "enia)"});
	CHECK(info.Documentation == std::string{kWriteLineSummary} + "\n\nWyj\xC4\x85" "tki:\nSystem.IO.IOException");
	CHECK(info.Raw == kRoslynWriteLine);
	CHECK(info.Kind.empty());
	CHECK(info.Name.empty());
}

TEST_CASE("OmniSharp's overload note is split off and its second code block reads as documentation")
{
	const Graphite::HoverInfo info = Graphite::ParseMarkdownHover(kOmniSharpWriteLine);

	CHECK(info.Declaration == "void System.Console.WriteLine(string? value)");
	CHECK(info.OverloadNote == std::string{"(+ 19 przeci\xC4\x85\xC5\xBC" "enia)"});
	CHECK(info.Documentation == std::string{kWriteLineSummary} + "\n\nWyj\xC4\x85" "tki:\n\nSystem.IO.IOException");
}

TEST_CASE("csharp-ls's lists lose their code marks and keep their items")
{
	const Graphite::HoverInfo info = Graphite::ParseMarkdownHover(kCsharpLsWriteLine);

	CHECK(info.Declaration == "void Console.WriteLine(string? value)");
	CHECK(info.OverloadNote.empty());
	CHECK(info.Documentation == std::string{kWriteLineSummary} +
									"\n\nParameters:\n- value: The value to write.\n\nExceptions:\n- System.IO.IOException: An I/O error occurred.");
}

TEST_CASE("A declaration alone has no documentation")
{
	const Graphite::HoverInfo info = Graphite::ParseMarkdownHover("```csharp\r\nstring Greeter.Hello(string name)\r\n```\r\n  \r\n");

	CHECK(info.Parsed);
	CHECK(info.Declaration == "string Greeter.Hello(string name)");
	CHECK(info.Documentation.empty());
}

TEST_CASE("A declaration over several lines keeps its lines")
{
	const Graphite::HoverInfo info = Graphite::ParseMarkdownHover("```csharp\nvoid Draw(\n    int width,\n    int height)\n```");

	CHECK(info.Declaration == "void Draw(\n    int width,\n    int height)");
	CHECK(info.OverloadNote.empty());
}

TEST_CASE("An overload note is taken off the last line of a declaration over several lines")
{
	const Graphite::HoverInfo info = Graphite::ParseMarkdownHover("```csharp\nvoid Draw(\n    int width) (+ 2 overloads)\n```");

	CHECK(info.Declaration == "void Draw(\n    int width)");
	CHECK(info.OverloadNote == "(+ 2 overloads)");
}

TEST_CASE("A parameter list closing a declaration is no overload note")
{
	const Graphite::HoverInfo plain = Graphite::ParseMarkdownHover("```csharp\nvoid Draw(int width, int height)\n```");
	const Graphite::HoverInfo signedCount = Graphite::ParseMarkdownHover("```csharp\nint Offset(+1)\n```");
	const Graphite::HoverInfo uncounted = Graphite::ParseMarkdownHover("```csharp\nvoid Draw(int width) (+ more)\n```");
	const Graphite::HoverInfo alone = Graphite::ParseMarkdownHover("```csharp\n(+ 1 overload)\n```");

	CHECK(plain.Declaration == "void Draw(int width, int height)");
	CHECK(plain.OverloadNote.empty());
	CHECK(signedCount.Declaration == "int Offset(+1)");
	CHECK(signedCount.OverloadNote.empty());
	CHECK(uncounted.Declaration == "void Draw(int width) (+ more)");
	CHECK(uncounted.OverloadNote.empty());
	CHECK(alone.Declaration == "(+ 1 overload)");
	CHECK(alone.OverloadNote.empty());
}

TEST_CASE("Every character reference the reader knows reads as its character")
{
	const Graphite::HoverInfo info = Graphite::ParseMarkdownHover("List&lt;T&gt; &quot;quoted&quot; it&#39;s salt &amp; pepper");

	CHECK(info.Documentation == "List<T> \"quoted\" it's salt & pepper");
}

TEST_CASE("An escaped backtick reads as a backtick while a bare one is a code mark")
{
	const Graphite::HoverInfo info = Graphite::ParseMarkdownHover("Press \\` then `Enter`");

	CHECK(info.Documentation == "Press ` then Enter");
}

TEST_CASE("Markdown with no code block is all documentation and empty markdown says nothing")
{
	const Graphite::HoverInfo text = Graphite::ParseMarkdownHover("Just a note\\.");
	const Graphite::HoverInfo nothing = Graphite::ParseMarkdownHover("");

	CHECK(text.Parsed);
	CHECK(text.Declaration.empty());
	CHECK(text.Documentation == "Just a note.");
	CHECK_FALSE(nothing.Parsed);
	CHECK(nothing.Raw.empty());
}
