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
	const std::filesystem::path file =
		temp.MakeFile("a.png", std::string("\x89PNG\r\n\x1a\n\0\0\0\r", 16));

	const Graphite::FileText read = Graphite::ReadFileText(file);

	CHECK_FALSE(read.Readable);
}

TEST_CASE("A UTF-8 file with a BOM is read the same way PreviewContent reads one")
{
	// Core::Decode does not strip a BOM by itself — Core/Text/TextDocument.cpp strips it
	// separately with BomLength() before decoding, for the one document that gets edited.
	// ReadFileText mirrors PreviewContent's read, which never takes that extra step, so the
	// mark comes back as part of the text rather than being stripped here.
	TempDirectory temp;
	const std::filesystem::path file = temp.MakeFile("a.cpp", "\xEF\xBB\xBFint x;");

	const Graphite::FileText read = Graphite::ReadFileText(file);

	REQUIRE(read.Readable);
	CHECK(read.Text == "\xEF\xBB\xBFint x;");
}
