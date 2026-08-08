#include <doctest/doctest.h>

#include "Core/Sln/TextEdit.h"

#include <string>

using namespace core::sln::text;

TEST_CASE("The line ending is taken from the file, not assumed")
{
    CHECK(LineEnding("<a>\r\n  <b/>\r\n</a>") == "\r\n");
    CHECK(LineEnding("<a>\n  <b/>\n</a>") == "\n");

    // A file with no line break at all still has to be written to somehow.
    CHECK(LineEnding("<a/>") == "\n");
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
