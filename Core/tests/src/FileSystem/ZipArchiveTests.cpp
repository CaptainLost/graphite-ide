#include <doctest/doctest.h>

#include "Core/FileSystem/FileSystem.h"
#include "Core/FileSystem/ZipArchive.h"
#include "Core/Jobs/CancellationToken.h"

#include "TempDirectory.h"
#include "ZipFixtures.h"

#include <filesystem>
#include <fstream>
#include <iterator>
#include <ostream>
#include <string>
#include <vector>

namespace {

	std::string ReadWholeFile(const std::filesystem::path& path)
	{
		std::ifstream stream{path, std::ios::binary};
		return std::string{std::istreambuf_iterator<char>{stream}, std::istreambuf_iterator<char>{}};
	}

	void WriteWholeFile(const std::filesystem::path& path, const std::string& bytes)
	{
		std::ofstream stream{path, std::ios::binary | std::ios::trunc};
		stream << bytes;
	}

	bool IsEmptyDirectory(const std::filesystem::path& directory)
	{
		return std::filesystem::is_directory(directory) && std::filesystem::is_empty(directory);
	}

	Core::Result<void> ExtractArchiveBytes(TempDirectory& temporaryDirectory, const std::string& archiveBytes,
										   const std::filesystem::path& destination)
	{
		const std::filesystem::path archive = temporaryDirectory.MakeFile("archive.zip", archiveBytes);
		return Core::ExtractZip(archive, destination, Core::CancellationToken{});
	}
}

TEST_CASE("Extracting a zip writes every entry below the destination with its directories")
{
	TempDirectory temporaryDirectory;
	const std::filesystem::path destination = temporaryDirectory.GetPath() / "1.0.tmp";

	const Core::Result<void> extracted = ExtractArchiveBytes(temporaryDirectory,
															 ZipBytes({
																 {.Name = "tool_1.0/bin/tool.exe", .Contents = "the program"},
																 {.Name = "tool_1.0/lib/include/stddef.h", .Contents = "a header"},
																 {.Name = "empty/", .Contents = ""},
															 }),
															 destination);

	REQUIRE(extracted);
	CHECK(ReadWholeFile(destination / "tool_1.0" / "bin" / "tool.exe") == "the program");
	CHECK(ReadWholeFile(destination / "tool_1.0" / "lib" / "include" / "stddef.h") == "a header");
	CHECK(IsEmptyDirectory(destination / "empty"));
}

TEST_CASE("An archive with one entry that climbs out extracts nothing at all")
{
	TempDirectory temporaryDirectory;
	const std::filesystem::path destination = temporaryDirectory.GetPath() / "inside";

	const Core::Result<void> extracted = ExtractArchiveBytes(temporaryDirectory,
															 ZipBytes({
																 {.Name = "good.txt", .Contents = "fine"},
																 {.Name = "../evil.txt", .Contents = "escaped"},
															 }),
															 destination);

	REQUIRE_FALSE(extracted);
	CHECK(extracted.GetError().Code == Core::ErrorCode::InvalidArgument);
	CHECK(extracted.GetError().Message.find("../evil.txt") != std::string::npos);
	CHECK_FALSE(std::filesystem::exists(destination));
	CHECK_FALSE(std::filesystem::exists(temporaryDirectory.GetPath() / "evil.txt"));
}

TEST_CASE("Entries that would escape or reach a stream or a device are refused before anything is written")
{
	TempDirectory temporaryDirectory;
	const std::filesystem::path destination = temporaryDirectory.GetPath() / "inside";
	std::string archiveBytes;

	SUBCASE("a rooted name")
	{
		archiveBytes = ZipBytes({{.Name = "good.txt", .Contents = "fine"}, {.Name = "Xevil.txt", .Contents = "escaped"}});
		RenameEntry(archiveBytes, "Xevil.txt", "/evil.txt");
	}

	SUBCASE("a drive letter")
	{
		archiveBytes = ZipBytes({{.Name = "good.txt", .Contents = "fine"}, {.Name = "C:/evil.txt", .Contents = "escaped"}});
	}

	SUBCASE("a climb written with backslashes")
	{
		archiveBytes = ZipBytes({{.Name = "good.txt", .Contents = "fine"}, {.Name = "..\\evil.txt", .Contents = "escaped"}});
	}

	SUBCASE("a climb from inside a directory")
	{
		archiveBytes = ZipBytes({{.Name = "good.txt", .Contents = "fine"}, {.Name = "bin/../../evil.txt", .Contents = "escaped"}});
	}

	SUBCASE("an alternate data stream")
	{
		archiveBytes = ZipBytes({{.Name = "good.txt", .Contents = "fine"}, {.Name = "good.txt:hidden", .Contents = "escaped"}});
	}

	SUBCASE("a device name")
	{
		archiveBytes = ZipBytes({{.Name = "good.txt", .Contents = "fine"}, {.Name = "bin/CON", .Contents = "escaped"}});
	}

	REQUIRE_FALSE(archiveBytes.empty());

	const Core::Result<void> extracted = ExtractArchiveBytes(temporaryDirectory, archiveBytes, destination);

	REQUIRE_FALSE(extracted);
	CHECK(extracted.GetError().Code == Core::ErrorCode::InvalidArgument);
	CHECK_FALSE(std::filesystem::exists(destination));
}

TEST_CASE("A destination outside the ANSI code page extracts")
{
	TempDirectory temporaryDirectory;
	const std::filesystem::path folder = temporaryDirectory.GetPath() / Core::FromUtf8("Zażółć Ω");
	std::filesystem::create_directories(folder);
	const std::filesystem::path archive = folder / Core::FromUtf8("archiwum ł.zip");
	WriteWholeFile(archive, ZipBytes({{.Name = "bin/tool.exe", .Contents = "the program"}}));
	const std::filesystem::path destination = folder / Core::FromUtf8("wersja ę");

	REQUIRE(Core::ExtractZip(archive, destination, Core::CancellationToken{}));

	CHECK(ReadWholeFile(destination / "bin" / "tool.exe") == "the program");
}

TEST_CASE("A cancelled extraction stops with Cancelled")
{
	TempDirectory temporaryDirectory;
	const std::filesystem::path archive =
		temporaryDirectory.MakeFile("archive.zip", ZipBytes({{.Name = "bin/tool.exe", .Contents = "the program"}}));
	Core::CancellationToken token;
	token.Cancel();

	const Core::Result<void> extracted = Core::ExtractZip(archive, temporaryDirectory.GetPath() / "inside", token);

	REQUIRE_FALSE(extracted);
	CHECK(extracted.GetError().Code == Core::ErrorCode::Cancelled);
	CHECK_FALSE(std::filesystem::exists(temporaryDirectory.GetPath() / "inside" / "bin" / "tool.exe"));
}

TEST_CASE("A file that is not a zip is reported rather than extracted")
{
	TempDirectory temporaryDirectory;
	const std::filesystem::path archive = temporaryDirectory.MakeFile("archive.zip", "this is not an archive");

	const Core::Result<void> extracted =
		Core::ExtractZip(archive, temporaryDirectory.GetPath() / "inside", Core::CancellationToken{});

	REQUIRE_FALSE(extracted);
	CHECK(extracted.GetError().Code == Core::ErrorCode::IoFailure);
	CHECK_FALSE(std::filesystem::exists(temporaryDirectory.GetPath() / "inside"));
}
