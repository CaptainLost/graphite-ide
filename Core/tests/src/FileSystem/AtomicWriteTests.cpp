#include <doctest/doctest.h>

#include "Core/FileSystem/AtomicWrite.h"
#include "Core/FileSystem/FileSystem.h"

#include "TempDirectory.h"

#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>
#include <vector>

#ifdef CORE_PLATFORM_WINDOWS
#include <windows.h>
#endif

using Core::WriteFileAtomically;

namespace {
	std::string ReadWholeFile(const std::filesystem::path& path)
	{
		std::ifstream stream{path, std::ios::binary};
		return std::string{std::istreambuf_iterator<char>{stream}, std::istreambuf_iterator<char>{}};
	}
}

TEST_CASE("Writing over an existing file leaves the new content")
{
	TempDirectory temporaryDirectory;
	const std::filesystem::path target = temporaryDirectory.MakeFile("target.txt", "original");

	REQUIRE(WriteFileAtomically(target, "replaced"));

	const std::string contents = ReadWholeFile(target);

	CHECK(contents == "replaced");
}

TEST_CASE("Writing a file that does not exist yet creates it")
{
	TempDirectory temporaryDirectory;
	const std::filesystem::path target = temporaryDirectory.GetPath() / "target.txt";

	REQUIRE(WriteFileAtomically(target, "content"));

	const std::string contents = ReadWholeFile(target);

	CHECK(contents == "content");
}

TEST_CASE("An occupied .tmp name is stepped over rather than clobbered")
{
	TempDirectory temporaryDirectory;
	const std::filesystem::path target = temporaryDirectory.MakeFile("target.txt", "original");
	const std::filesystem::path occupied = temporaryDirectory.MakeFile("target.txt.tmp", "someone else's data");

	REQUIRE(WriteFileAtomically(target, "replaced"));

	const std::string occupiedContents = ReadWholeFile(occupied);

	CHECK(occupiedContents == "someone else's data");

	const std::string targetContents = ReadWholeFile(target);

	CHECK(targetContents == "replaced");
}

TEST_CASE("A write to a missing directory fails and leaves nothing behind")
{
	TempDirectory temporaryDirectory;
	const std::filesystem::path target = temporaryDirectory.GetPath() / "missing-directory" / "target.txt";

	CHECK_FALSE(WriteFileAtomically(target, "content"));
	CHECK_FALSE(std::filesystem::exists(target));
	CHECK_FALSE(std::filesystem::exists(temporaryDirectory.GetPath() / "missing-directory"));
}

#ifdef CORE_PLATFORM_WINDOWS
TEST_CASE("A write to a target another program holds open fails and leaves the target and its content untouched with no temporary behind")
{
	TempDirectory temporaryDirectory;
	const std::filesystem::path target = temporaryDirectory.MakeFile("target.txt", "original");

	const DWORD shareNothing = 0;
	const HANDLE heldOpenByAnotherProgram = ::CreateFileW(target.c_str(), GENERIC_READ, shareNothing, nullptr, OPEN_EXISTING,
														  FILE_ATTRIBUTE_NORMAL, nullptr);
	REQUIRE(heldOpenByAnotherProgram != INVALID_HANDLE_VALUE);

	CHECK_FALSE(WriteFileAtomically(target, "replaced"));

	::CloseHandle(heldOpenByAnotherProgram);

	const std::string contents = ReadWholeFile(target);
	CHECK(contents == "original");

	// The whole listing is compared rather than each name checked in a loop, because a
	// directory that failed to open lists nothing, and a loop over nothing checks nothing.
	std::error_code listingError;
	std::vector<std::string> fileNames;

	for (const std::filesystem::directory_entry& entry : std::filesystem::directory_iterator{temporaryDirectory.GetPath(), listingError})
	{
		fileNames.push_back(Core::ToUtf8(entry.path().filename()));
	}

	CHECK_FALSE(listingError);
	CHECK(fileNames == std::vector<std::string>{"target.txt"});
}

TEST_CASE("An alternate data stream survives an atomic write")
{
	TempDirectory temporaryDirectory;
	const std::filesystem::path target = temporaryDirectory.GetPath() / "target.txt";

	{
		std::ofstream content{target, std::ios::binary};
		content << "original";
	}
	{
		std::ofstream alternateStream{target.string() + ":meta", std::ios::binary};
		alternateStream << "kept";
	}

	REQUIRE(WriteFileAtomically(target, "replaced"));

	std::ifstream alternateStream{target.string() + ":meta", std::ios::binary};
	std::string recovered;
	alternateStream >> recovered;

	CHECK(recovered == "kept");
}
#endif

#if defined(CORE_PLATFORM_LINUX)
TEST_CASE("On Linux, rewriting a file keeps its permissions, so a script stays executable")
{
	TempDirectory directory;
	const std::filesystem::path script = directory.MakeFile("build.sh", "#!/bin/sh\n");
	const auto executable = std::filesystem::perms::owner_all | std::filesystem::perms::group_read |
							std::filesystem::perms::group_exec | std::filesystem::perms::others_read |
							std::filesystem::perms::others_exec;
	std::filesystem::permissions(script, executable);

	REQUIRE(WriteFileAtomically(script, "#!/bin/sh\necho hi\n").HasValue());

	CHECK(std::filesystem::status(script).permissions() == executable);
}
#endif
