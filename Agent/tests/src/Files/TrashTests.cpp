#include <doctest/doctest.h>

#include "Files/Trash.h"

#include "TempDirectory.h"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

namespace {

	std::string Read(const std::filesystem::path& file)
	{
		std::ifstream stream{file, std::ios::binary};
		std::ostringstream contents;
		contents << stream.rdbuf();
		return contents.str();
	}
}

TEST_CASE("A trashed file leaves its place and lands in the trash with a note of where it was")
{
	TempDirectory directory;
	const std::filesystem::path item = directory.MakeFile("a b%.txt", "contents");
	const std::filesystem::path trash = directory.GetPath() / "Trash";

	const Core::Result<std::filesystem::path> trashed = Agent::MoveToTrash(item, trash);

	REQUIRE(trashed.HasValue());
	CHECK_FALSE(std::filesystem::exists(item));
	CHECK(trashed.GetValue() == trash / "files" / "a b%.txt");
	CHECK(Read(trashed.GetValue()) == "contents");

	const std::string info = Read(trash / "info" / "a b%.txt.trashinfo");
	CHECK(info.starts_with("[Trash Info]\n"));
	CHECK(info.find("Path=" + (directory.GetPath() / "a%20b%25.txt").string() + "\n") != std::string::npos); // not a path conversion
	CHECK(info.find("DeletionDate=") != std::string::npos);
}

TEST_CASE("Two trashed files of one name are both kept")
{
	TempDirectory directory;
	const std::filesystem::path trash = directory.GetPath() / "Trash";

	const auto first = Agent::MoveToTrash(directory.MakeFile("one/a.txt", "1"), trash);
	const auto second = Agent::MoveToTrash(directory.MakeFile("two/a.txt", "2"), trash);

	REQUIRE(first.HasValue());
	REQUIRE(second.HasValue());
	CHECK(first.GetValue().filename() == "a.txt");
	CHECK(second.GetValue().filename() == "a.txt 2");
	CHECK(Read(second.GetValue()) == "2");
	CHECK(std::filesystem::exists(trash / "info" / "a.txt 2.trashinfo"));
}

TEST_CASE("A folder goes to the trash with everything in it")
{
	TempDirectory directory;
	directory.MakeFile("folder/inner/x.txt", "x");
	const std::filesystem::path trash = directory.GetPath() / "Trash";

	const auto trashed = Agent::MoveToTrash(directory.GetPath() / "folder", trash);

	REQUIRE(trashed.HasValue());
	CHECK(Read(trashed.GetValue() / "inner" / "x.txt") == "x");
}

TEST_CASE("Trashing what is not there is not found")
{
	TempDirectory directory;

	const auto trashed = Agent::MoveToTrash(directory.GetPath() / "missing.txt", directory.GetPath() / "Trash");

	REQUIRE_FALSE(trashed.HasValue());
	CHECK(trashed.GetError().Code == Core::ErrorCode::NotFound);
}

TEST_CASE("The percent-encoding keeps unreserved characters and encodes the rest byte by byte")
{
	CHECK(Agent::PercentEncodePath("/home/me/a b/ż.txt") == "/home/me/a%20b/%C5%BC.txt");
	CHECK(Agent::PercentEncodePath("/a-b_c.d~e") == "/a-b_c.d~e");
}
