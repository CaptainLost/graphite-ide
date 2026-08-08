#include <doctest/doctest.h>

#include "Core/Text/TextDocument.h"

#include <ostream>
#include <string>

using namespace core::text;

namespace
{
    TextDocument Make(std::string bytes)
    {
        auto document = TextDocument::FromContent(std::move(bytes), "C:/tmp/sample.txt");
        REQUIRE(document);

        return std::move(document).Value();
    }
}

TEST_CASE("A plain LF file loads as it is")
{
    const TextDocument document = Make("one\ntwo\n");

    CHECK(document.Buffer().Text() == "one\ntwo\n");
    CHECK(document.DocumentLineEnding() == LineEnding::Lf);
    CHECK_FALSE(document.WasMixed());
    CHECK(document.FileEncoding() == Encoding::Utf8);
    CHECK_FALSE(document.IsReadOnly());
}

TEST_CASE("CRLF is normalised away inside the buffer")
{
    const TextDocument document = Make("one\r\ntwo\r\n");

    // The buffer never holds a '\r', so nothing downstream has to know CRLF exists.
    CHECK(document.Buffer().Text() == "one\ntwo\n");
    CHECK(document.Buffer().LineCount() == 3);
    CHECK(document.DocumentLineEnding() == LineEnding::Crlf);
}

TEST_CASE("A CRLF file is written back as CRLF")
{
    const TextDocument document = Make("one\r\ntwo\r\n");

    CHECK(document.Bytes() == "one\r\ntwo\r\n");
}

TEST_CASE("An untouched file comes back byte for byte")
{
    const std::string original = "int main()\r\n{\r\n    return 0;\r\n}\r\n";

    CHECK(Make(original).Bytes() == original);
}

TEST_CASE("A file with both styles takes the majority and says it was mixed")
{
    const TextDocument document = Make("a\r\nb\r\nc\r\nd\ne\r\n");

    CHECK(document.DocumentLineEnding() == LineEnding::Crlf);
    CHECK(document.WasMixed());

    // Saving unifies, which is why the status bar has to show it beforehand.
    CHECK(document.Bytes() == "a\r\nb\r\nc\r\nd\r\ne\r\n");
}

TEST_CASE("A mostly-LF file with one CRLF takes LF")
{
    const TextDocument document = Make("a\nb\nc\r\nd\n");

    CHECK(document.DocumentLineEnding() == LineEnding::Lf);
    CHECK(document.WasMixed());
}

TEST_CASE("A file with no line break at all is LF")
{
    const TextDocument document = Make("no newline here");

    CHECK(document.DocumentLineEnding() == LineEnding::Lf);
    CHECK_FALSE(document.WasMixed());
    CHECK(document.Bytes() == "no newline here");
}

TEST_CASE("An empty file loads as one empty line")
{
    const TextDocument document = Make("");

    CHECK(document.Buffer().LineCount() == 1);
    CHECK(document.Bytes() == "");
}

TEST_CASE("A file of nothing but newlines keeps every one of them")
{
    const TextDocument document = Make("\n\n\n");

    CHECK(document.Buffer().LineCount() == 4);
    CHECK(document.Bytes() == "\n\n\n");
}

TEST_CASE("A UTF-8 byte order mark is stripped from the content and put back on save")
{
    const TextDocument document = Make("\xEF\xBB\xBFint main() {}");

    // Left in the content it would be an invisible character in line 1, throwing off
    // every column and showing the file as changed in git the moment it was saved.
    CHECK(document.Buffer().Text() == "int main() {}");
    CHECK(document.FileEncoding() == Encoding::Utf8Bom);
    CHECK(document.Bytes() == "\xEF\xBB\xBFint main() {}");
}

TEST_CASE("A Visual Studio file with a mark and CRLF round-trips exactly")
{
    const std::string original = "\xEF\xBB\xBF<Project>\r\n</Project>\r\n";

    CHECK(Make(original).Bytes() == original);
}

TEST_CASE("Content that is not valid UTF-8 loads read-only with its bytes untouched")
{
    // "część" as Windows-1250 writes it.
    const std::string original = "cz\xEA\x9C\xE6\r\nnext line\r\n";
    const TextDocument document = Make(original);

    CHECK(document.IsReadOnly());
    CHECK(document.FileEncoding() == Encoding::Unknown);
    CHECK(document.Bytes() == original);
}

TEST_CASE("A UTF-16 file is refused by name rather than loaded")
{
    std::string bytes = "\xFF\xFE";
    bytes += std::string("h\0e\0l\0l\0o\0", 10);

    const auto document = TextDocument::FromContent(bytes, "C:/tmp/utf16.txt");

    REQUIRE_FALSE(document);
    CHECK(document.GetError().message.find("UTF-16") != std::string::npos);
}

TEST_CASE("The document remembers where it came from")
{
    const TextDocument document = Make("hello");

    CHECK(document.Path() == std::filesystem::path{"C:/tmp/sample.txt"});
}

TEST_CASE("A freshly loaded document is not modified")
{
    const TextDocument document = Make("hello");

    CHECK_FALSE(document.IsModified());
}

TEST_CASE("Editing modifies, and undoing all the way back clears it again")
{
    TextDocument document = Make("hello");

    document.Buffer().Insert(5, " world");
    CHECK(document.IsModified());

    document.Buffer().Undo();
    CHECK_FALSE(document.IsModified());
}

TEST_CASE("Marking saved moves the point that modification is measured from")
{
    TextDocument document = Make("hello");

    document.Buffer().Insert(5, "!");
    document.MarkSaved();
    CHECK_FALSE(document.IsModified());

    document.Buffer().Undo();
    CHECK(document.IsModified());

    document.Buffer().Redo();
    CHECK_FALSE(document.IsModified());
}

TEST_CASE("Edits made after saving are written back with the file's own line ending")
{
    TextDocument document = Make("one\r\ntwo\r\n");

    document.Buffer().Insert(document.Buffer().Length(), "three\n");

    CHECK(document.Bytes() == "one\r\ntwo\r\nthree\r\n");
}
