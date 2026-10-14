#include <doctest/doctest.h>

#include "Language/Analysis/FileTextSource.h"

#include "TempDirectory.h"

#include <chrono>
#include <optional>
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

	constexpr std::size_t pngSignatureAndHeaderChunkStartLength = 16;
	const std::filesystem::path file =
		temp.MakeFile("a.png", std::string("\x89PNG\r\n\x1a\n\0\0\0\x0dIHDR", pngSignatureAndHeaderChunkStartLength));

	const Graphite::FileText read = Graphite::ReadFileText(file);

	CHECK_FALSE(read.Readable);
}

TEST_CASE("A UTF-8 file with a BOM comes back without it, so colours on line 0 line up with the editor's own buffer")
{
	TempDirectory temp;
	const std::filesystem::path file = temp.MakeFile("a.cpp", "\xEF\xBB\xBFint x;");

	const Graphite::FileText read = Graphite::ReadFileText(file);

	REQUIRE(read.Readable);
	CHECK(read.Text == "int x;");
}

TEST_CASE("A file whose stamp moved between the stamp before the read and the stamp after it is not vouched for, as a save landing in between would pair old text with a new time")
{
	TempDirectory temp;
	const std::filesystem::path file = temp.MakeFile("a.cpp", "int main() {}\n");

	const std::filesystem::file_time_type originalStamp = std::filesystem::last_write_time(file);
	int stampReads = 0;

	const Graphite::FileText moved = Graphite::ReadFileText(
		file,
		[&](const std::filesystem::path&) -> std::optional<std::filesystem::file_time_type> {
			return originalStamp + std::chrono::seconds(stampReads++);
		});

	CHECK_FALSE(moved.Readable);
	CHECK(moved.Text.empty());
	CHECK(stampReads == 2);

	const Graphite::FileText steady = Graphite::ReadFileText(
		file,
		[&](const std::filesystem::path&) -> std::optional<std::filesystem::file_time_type> {
			return originalStamp;
		});

	CHECK(steady.Readable);
	CHECK(steady.ModifiedAt == originalStamp);
}

TEST_CASE("A file with no stamp before the read is not readable")
{
	TempDirectory temp;
	const std::filesystem::path file = temp.MakeFile("a.cpp", "int x;");

	const Graphite::FileText read = Graphite::ReadFileText(
		file,
		[](const std::filesystem::path&) -> std::optional<std::filesystem::file_time_type> {
			return std::nullopt;
		});

	CHECK_FALSE(read.Readable);
}
