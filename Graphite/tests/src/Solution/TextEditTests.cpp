#include <doctest/doctest.h>

#include "Solution/TextEdit.h"

#include <string>

using namespace Graphite;

TEST_CASE("The line ending is taken from the file, not assumed, and a file with no line break falls back to a line feed")
{
	CHECK(DetectLineEnding("<a>\r\n  <b/>\r\n</a>") == "\r\n");
	CHECK(DetectLineEnding("<a>\n  <b/>\n</a>") == "\n");

	CHECK(DetectLineEnding("<a/>") == "\n");
}

TEST_CASE("The indent unit is taken from the file, and a file with nothing to learn from gets the two spaces premake writes")
{
	CHECK(IndentUnit("<a>\n  <b/>\n</a>") == "  ");
	CHECK(IndentUnit("<a>\n    <b/>\n</a>") == "    ");
	CHECK(IndentUnit("<a>\n\t<b/>\n</a>") == "\t");

	CHECK(IndentUnit("<a/>") == "  ");
}

TEST_CASE("The opening tag ends after its closing angle bracket, counting offsets from one past the opening bracket")
{
	const std::string text = R"(<Folder Name="/App/">)";

	CHECK(OpeningTagEnd(text, 1) == text.size());
}

TEST_CASE("An angle bracket inside an attribute does not end the tag, so a splice cannot land inside an attribute")
{
	const std::string text = R"(<Project Path="a>b.vcxproj" />after)";

	CHECK(OpeningTagEnd(text, 1) == text.find("after"));
}

TEST_CASE("A self-closing element ends at its own slash-bracket")
{
	const std::string text = R"(<Project Path="a" />  <Project Path="b" />)";

	CHECK(ElementEnd(text, 1, "Project") == text.find("  <Project"));
}

TEST_CASE("An element with a closing tag ends after it")
{
	const std::string text = R"(<Project Path="a"></Project>tail)";

	CHECK(ElementEnd(text, 1, "Project") == text.find("tail"));
}

TEST_CASE("An element with children ends after its own closing tag")
{
	const std::string text = "<Folder Name=\"x\">\n  <Project Path=\"a\" />\n</Folder>tail";

	CHECK(ElementEnd(text, 1, "Folder") == text.find("tail"));
}

TEST_CASE("A closing tag whose name only starts the same does not end the element, so a splice cannot land inside the child")
{
	const std::string text = R"(<Folder Name="a"><FolderSet Name="b"></FolderSet></Folder>tail)";

	CHECK(ElementEnd(text, 1, "Folder") == text.find("tail"));
}

TEST_CASE("A self-closing child of the same name does not raise the nesting depth, which would run the scan to the end of the text")
{
	const std::string text = R"(<Folder Name="a"><Folder Name="b" /></Folder>tail)";

	CHECK(ElementEnd(text, 1, "Folder") == text.find("tail"));
}

TEST_CASE("The line extent covers the whole line and its ending")
{
	const std::string text = "one\r\ntwo\r\nthree";

	const auto [begin, end] = LineExtent(text, text.find("two"));

	CHECK(begin == 5);
	CHECK(end == text.find("three"));
}

TEST_CASE("The last line has no ending to include")
{
	const std::string text = "one\ntwo";

	const auto [begin, end] = LineExtent(text, text.find("two"));

	CHECK(begin == 4);
	CHECK(end == text.size());
}
