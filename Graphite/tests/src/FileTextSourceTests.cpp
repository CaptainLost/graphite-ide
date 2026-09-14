#include <doctest/doctest.h>

#include "Lang/Analysis/FileTextSource.h"

#include "TempDirectory.h"

#include <string>

TEST_CASE("A text file is read, decoded and stamped")
{
	TempDirectory temp;
	const std::filesystem::path file = temp.MakeFile("a.cpp", "int main() {}\n");

	const Graphite::FileText read = Graphite::ReadFileText(file);

	CHECK(read.Readable);
	CHECK(read.Path == file);
	CHECK(read.Text == "int main() {}\n");
	CHECK(read.ModifiedAt == std::filesystem::last_write_time(file));
}

TEST_CASE("A file that is not there is not readable, and says so rather than throwing")
{
	TempDirectory temp;

	const Graphite::FileText read = Graphite::ReadFileText(temp.GetPath() / "missing.cpp");

	CHECK_FALSE(read.Readable);
	CHECK(read.Text.empty());
}

TEST_CASE("A binary file is refused rather than sent to a server as text")
{
	TempDirectory temp;

	// A real PNG signature plus the length and chunk-type fields that start every IHDR
	// chunk: 16 explicit bytes, so the std::string constructor below reads exactly what is
	// there rather than running past the end of a shorter literal.
	const std::filesystem::path file =
		temp.MakeFile("a.png", std::string("\x89PNG\r\n\x1a\n\0\0\0\x0dIHDR", 16));

	const Graphite::FileText read = Graphite::ReadFileText(file);

	CHECK_FALSE(read.Readable);
}

TEST_CASE("A UTF-8 file with a BOM comes back without it")
{
	// Core::Decode passes a UTF-8 BOM through unchanged — it is ReadFileText that strips it
	// afterwards, the same mark TextDocument::FromContent strips from the buffer this text's
	// colours are eventually drawn against. Left in, every colour on line 0 would be one
	// UTF-16 unit off from where the editor's own buffer puts that line's text.
	TempDirectory temp;
	const std::filesystem::path file = temp.MakeFile("a.cpp", "\xEF\xBB\xBFint x;");

	const Graphite::FileText read = Graphite::ReadFileText(file);

	REQUIRE(read.Readable);
	CHECK(read.Text == "int x;");
}
