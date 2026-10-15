#include <doctest/doctest.h>

#include "Core/FileSystem/FileOperations.h"

#include "TempDirectory.h"

#include <fstream>

using Core::CreateEmptyFile;
using Core::Duplicate;
using Core::MakeDirectory;
using Core::MakeUniquePath;
using Core::MoveToRecycleBin;
using Core::Rename;

TEST_CASE("MakeUniquePath returns the original path when nothing collides")
{
	TempDirectory temporaryDirectory;
	const std::filesystem::path freePath = temporaryDirectory.GetPath() / "main.cpp";

	CHECK(MakeUniquePath(freePath) == freePath);
}

TEST_CASE("MakeUniquePath appends copy on the first collision")
{
	TempDirectory temporaryDirectory;
	temporaryDirectory.MakeFile("main.cpp", "x");

	CHECK(MakeUniquePath(temporaryDirectory.GetPath() / "main.cpp") == temporaryDirectory.GetPath() / "main copy.cpp");
}

TEST_CASE("MakeUniquePath numbers further collisions")
{
	TempDirectory temporaryDirectory;
	temporaryDirectory.MakeFile("main.cpp", "x");
	temporaryDirectory.MakeFile("main copy.cpp", "x");
	temporaryDirectory.MakeFile("main copy 2.cpp", "x");

	CHECK(MakeUniquePath(temporaryDirectory.GetPath() / "main.cpp") == temporaryDirectory.GetPath() / "main copy 3.cpp");
}

TEST_CASE("MakeUniquePath handles a name without an extension")
{
	TempDirectory temporaryDirectory;
	temporaryDirectory.MakeDirectory("build");

	CHECK(MakeUniquePath(temporaryDirectory.GetPath() / "build") == temporaryDirectory.GetPath() / "build copy");
}

TEST_CASE("MakeUniquePath treats a dotfile as a name, not an extension")
{
	TempDirectory temporaryDirectory;
	temporaryDirectory.MakeFile(".gitignore", "x");

	CHECK(MakeUniquePath(temporaryDirectory.GetPath() / ".gitignore") == temporaryDirectory.GetPath() / ".gitignore copy");
}

TEST_CASE("MakeUniquePath splits a compound extension at its last dot the way std::filesystem does rather than at its first")
{
	TempDirectory temporaryDirectory;
	temporaryDirectory.MakeFile("archive.tar.gz", "x");

	CHECK(MakeUniquePath(temporaryDirectory.GetPath() / "archive.tar.gz") == temporaryDirectory.GetPath() / "archive.tar copy.gz");
}

TEST_CASE("CreateEmptyFile creates a file and refuses to overwrite one")
{
	TempDirectory temporaryDirectory;
	const std::filesystem::path target = temporaryDirectory.GetPath() / "fresh.txt";

	REQUIRE(CreateEmptyFile(target).HasValue());
	CHECK(std::filesystem::exists(target));

	const Core::Result<void> secondCreate = CreateEmptyFile(target);
	REQUIRE_FALSE(secondCreate.HasValue());
	CHECK(secondCreate.GetError().Code == Core::ErrorCode::InvalidArgument);
}

TEST_CASE("MakeDirectory creates a directory and refuses an occupied name")
{
	TempDirectory temporaryDirectory;
	const std::filesystem::path target = temporaryDirectory.GetPath() / "sub";

	REQUIRE(MakeDirectory(target).HasValue());
	CHECK(std::filesystem::is_directory(target));

	const Core::Result<void> secondMake = MakeDirectory(target);
	REQUIRE_FALSE(secondMake.HasValue());
	CHECK(secondMake.GetError().Code == Core::ErrorCode::InvalidArgument);
}

TEST_CASE("Rename moves a file to the new name")
{
	TempDirectory temporaryDirectory;
	const std::filesystem::path source = temporaryDirectory.MakeFile("before.txt", "payload");
	const std::filesystem::path destination = temporaryDirectory.GetPath() / "after.txt";

	REQUIRE(Rename(source, destination).HasValue());

	CHECK_FALSE(std::filesystem::exists(source));
	REQUIRE(std::filesystem::exists(destination));
}

TEST_CASE("Rename refuses to clobber an existing target")
{
	TempDirectory temporaryDirectory;
	const std::filesystem::path source = temporaryDirectory.MakeFile("before.txt", "a");
	const std::filesystem::path destination = temporaryDirectory.MakeFile("taken.txt", "b");

	const Core::Result<void> renameResult = Rename(source, destination);

	REQUIRE_FALSE(renameResult.HasValue());
	CHECK(renameResult.GetError().Code == Core::ErrorCode::InvalidArgument);
	CHECK(std::filesystem::exists(source));
}

TEST_CASE("Rename reports a missing source and creates no destination")
{
	TempDirectory temporaryDirectory;

	const Core::Result<void> renameResult = Rename(temporaryDirectory.GetPath() / "ghost.txt", temporaryDirectory.GetPath() / "target.txt");

	REQUIRE_FALSE(renameResult.HasValue());
	CHECK(renameResult.GetError().Code == Core::ErrorCode::NotFound);
	CHECK_FALSE(std::filesystem::exists(temporaryDirectory.GetPath() / "target.txt"));
}

TEST_CASE("Duplicate copies a file beside the original and returns the new path")
{
	TempDirectory temporaryDirectory;
	const std::filesystem::path original = temporaryDirectory.MakeFile("main.cpp", "content");

	const Core::Result<std::filesystem::path> duplicateResult = Duplicate(original);

	REQUIRE(duplicateResult.HasValue());
	CHECK(duplicateResult.GetValue() == temporaryDirectory.GetPath() / "main copy.cpp");
	REQUIRE(std::filesystem::exists(duplicateResult.GetValue()));

	std::ifstream stream{duplicateResult.GetValue(), std::ios::binary};
	const std::string contents{std::istreambuf_iterator<char>{stream},
							   std::istreambuf_iterator<char>{}};
	CHECK(contents == "content");
}

TEST_CASE("Duplicate copies a directory recursively")
{
	TempDirectory temporaryDirectory;
	temporaryDirectory.MakeFile("sub/inner.txt", "x");

	const Core::Result<std::filesystem::path> duplicateResult = Duplicate(temporaryDirectory.GetPath() / "sub");

	REQUIRE(duplicateResult.HasValue());
	CHECK(duplicateResult.GetValue() == temporaryDirectory.GetPath() / "sub copy");
	CHECK(std::filesystem::exists(duplicateResult.GetValue() / "inner.txt"));
}

TEST_CASE("Duplicate reports a missing source")
{
	TempDirectory temporaryDirectory;

	const Core::Result<std::filesystem::path> duplicateResult = Duplicate(temporaryDirectory.GetPath() / "ghost.txt");

	REQUIRE_FALSE(duplicateResult.HasValue());
	CHECK(duplicateResult.GetError().Code == Core::ErrorCode::NotFound);
}

TEST_CASE("MoveToRecycleBin removes a file from its path and leaves where it goes to the Recycle Bin")
{
	TempDirectory temporaryDirectory;
	const std::filesystem::path fileToRecycle = temporaryDirectory.MakeFile("doomed.txt", "x");

	REQUIRE(MoveToRecycleBin(fileToRecycle).HasValue());

	CHECK_FALSE(std::filesystem::exists(fileToRecycle));
}

TEST_CASE("MoveToRecycleBin reports a missing path")
{
	TempDirectory temporaryDirectory;

	const Core::Result<void> recycleResult = MoveToRecycleBin(temporaryDirectory.GetPath() / "ghost.txt");

	REQUIRE_FALSE(recycleResult.HasValue());
	CHECK(recycleResult.GetError().Code == Core::ErrorCode::NotFound);
}
