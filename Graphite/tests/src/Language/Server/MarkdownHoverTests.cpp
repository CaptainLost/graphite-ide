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

TEST_CASE("Roslyn's hover reads as its declaration and its documentation without escapes")
{
	const Graphite::HoverInfo info = Graphite::ParseMarkdownHover(kRoslynWriteLine);

	CHECK(info.Parsed);
	CHECK(info.Declaration == std::string{"void System.Console.WriteLine(string? value) (+ 19 przeci\xC4\x85\xC5\xBC" "enia)"});
	CHECK(info.Documentation == std::string{kWriteLineSummary} + "\n\nWyj\xC4\x85" "tki:\nSystem.IO.IOException");
	CHECK(info.Raw == kRoslynWriteLine);
	CHECK(info.Kind.empty());
	CHECK(info.Name.empty());
}

TEST_CASE("OmniSharp's second code block reads as documentation")
{
	const Graphite::HoverInfo info = Graphite::ParseMarkdownHover(kOmniSharpWriteLine);

	CHECK(info.Declaration == std::string{"void System.Console.WriteLine(string? value) (+ 19 przeci\xC4\x85\xC5\xBC" "enia)"});
	CHECK(info.Documentation == std::string{kWriteLineSummary} + "\n\nWyj\xC4\x85" "tki:\n\nSystem.IO.IOException");
}

TEST_CASE("csharp-ls's lists lose their code marks and keep their items")
{
	const Graphite::HoverInfo info = Graphite::ParseMarkdownHover(kCsharpLsWriteLine);

	CHECK(info.Declaration == "void Console.WriteLine(string? value)");
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
