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

TEST_CASE("Writing over an existing file leaves the new content")
{
	TempDirectory temp;
	const auto path = temp.MakeFile("target.txt", "original");

	REQUIRE(WriteFileAtomically(path, "replaced"));

	std::ifstream stream{path, std::ios::binary};
	std::string contents{std::istreambuf_iterator<char>{stream}, std::istreambuf_iterator<char>{}};

	CHECK(contents == "replaced");
}

TEST_CASE("Writing a file that does not exist yet creates it")
{
	TempDirectory temp;
	const auto path = temp.GetPath() / "target.txt";

	REQUIRE(WriteFileAtomically(path, "content"));

	std::ifstream stream{path, std::ios::binary};
	std::string contents{std::istreambuf_iterator<char>{stream}, std::istreambuf_iterator<char>{}};

	CHECK(contents == "content");
}

TEST_CASE("An occupied .tmp name is stepped over rather than clobbered")
{
	TempDirectory temp;
	const auto path = temp.MakeFile("target.txt", "original");
	const auto occupied = temp.MakeFile("target.txt.tmp", "someone else's data");

	REQUIRE(WriteFileAtomically(path, "replaced"));

	std::ifstream stream{occupied, std::ios::binary};
	std::string contents{std::istreambuf_iterator<char>{stream}, std::istreambuf_iterator<char>{}};

	CHECK(contents == "someone else's data");

	std::ifstream targetStream{path, std::ios::binary};
	std::string targetContents{std::istreambuf_iterator<char>{targetStream},
							   std::istreambuf_iterator<char>{}};

	CHECK(targetContents == "replaced");
}

TEST_CASE("A write to a missing directory fails and leaves nothing behind")
{
	TempDirectory temp;
	const auto path = temp.GetPath() / "missing-directory" / "target.txt";

	CHECK_FALSE(WriteFileAtomically(path, "content"));
	CHECK_FALSE(std::filesystem::exists(path));
	CHECK_FALSE(std::filesystem::exists(temp.GetPath() / "missing-directory"));
}

#ifdef CORE_PLATFORM_WINDOWS
TEST_CASE("A failed write leaves an existing target and its content untouched")
{
	TempDirectory temp;
	const auto path = temp.MakeFile("target.txt", "original");

	// Opened with no sharing at all, so ReplaceFileW cannot get at it: this is the
	// "file held open by another program" case ErrorCodeFor exists to name, provoked
	// deterministically against real content rather than the missing-directory case
	// covered above, which never has a target to protect in the first place.
	const HANDLE handle = ::CreateFileW(path.c_str(), GENERIC_READ, 0, nullptr, OPEN_EXISTING,
										FILE_ATTRIBUTE_NORMAL, nullptr);
	REQUIRE(handle != INVALID_HANDLE_VALUE);

	CHECK_FALSE(WriteFileAtomically(path, "replaced"));

	::CloseHandle(handle);

	std::ifstream stream{path, std::ios::binary};
	std::string contents{std::istreambuf_iterator<char>{stream}, std::istreambuf_iterator<char>{}};
	CHECK(contents == "original");

	// No debris either: the temporary that held "replaced" must be gone. The count is
	// asserted rather than only the names, because an iterator that failed to open the
	// directory yields nothing at all — a loop body that never runs would let this pass
	// having checked nothing, which is exactly the shape of a test that quietly stops
	// testing.
	std::error_code code;
	std::vector<std::string> names;

	for (const auto& entry : std::filesystem::directory_iterator{temp.GetPath(), code})
	{
		names.push_back(Core::ToUtf8(entry.path().filename()));
	}

	CHECK_FALSE(code);
	CHECK(names == std::vector<std::string>{"target.txt"});
}

TEST_CASE("An alternate data stream survives an atomic write")
{
	TempDirectory temp;
	const std::filesystem::path target = temp.GetPath() / "target.txt";

	{
		std::ofstream content{target, std::ios::binary};
		content << "original";
	}
	{
		std::ofstream stream{target.string() + ":meta", std::ios::binary};
		stream << "kept";
	}

	REQUIRE(WriteFileAtomically(target, "replaced"));

	std::ifstream stream{target.string() + ":meta", std::ios::binary};
	std::string recovered;
	stream >> recovered;

	CHECK(recovered == "kept");
}
#endif
