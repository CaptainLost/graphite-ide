#include <doctest/doctest.h>

#include "Core/Sln/TextEdit.h"

#include <string>

using namespace Core;

TEST_CASE("The line ending is taken from the file, not assumed")
{
    CHECK(DetectLineEnding("<a>\r\n  <b/>\r\n</a>") == "\r\n");
    CHECK(DetectLineEnding("<a>\n  <b/>\n</a>") == "\n");

    // A file with no line break at all still has to be written to somehow.
    CHECK(DetectLineEnding("<a/>") == "\n");
}

TEST_CASE("The indent unit is taken from the file")
{
    CHECK(IndentUnit("<a>\n  <b/>\n</a>") == "  ");
    CHECK(IndentUnit("<a>\n    <b/>\n</a>") == "    ");
    CHECK(IndentUnit("<a>\n\t<b/>\n</a>") == "\t");

    // Nothing to learn from, so two spaces — what premake writes.
    CHECK(IndentUnit("<a/>") == "  ");
}

TEST_CASE("The opening tag ends after its closing angle bracket")
{
    const std::string text = R"(<Folder Name="/App/">)";

    // Offsets are pugixml's: one past the '<'.
    CHECK(OpeningTagEnd(text, 1) == text.size());
}

TEST_CASE("An angle bracket inside an attribute does not end the tag")
{
    const std::string text = R"(<Project Path="a>b.vcxproj" />after)";

    // The naive scan for '>' stops at the one inside the quotes and produces a truncated
    // tag — which then splices text into the middle of an attribute.
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

TEST_CASE("A closing tag whose name only starts the same does not end the element")
{
    const std::string text = R"(<Folder Name="a"><FolderSet Name="b"></FolderSet></Folder>tail)";

    // "</FolderSet>" is not "</Folder>". Matching it by prefix ends the element early, and
    // a caller splicing at that offset writes into the middle of the child.
    CHECK(ElementEnd(text, 1, "Folder") == text.find("tail"));
}

TEST_CASE("A self-closing child of the same name does not raise the nesting depth")
{
    const std::string text = R"(<Folder Name="a"><Folder Name="b" /></Folder>tail)";

    // The child never closes, so counting it leaves the depth one too high and the scan
    // runs off the end — returning the whole text, which a caller then erases.
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
