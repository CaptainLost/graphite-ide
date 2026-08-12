#include <doctest/doctest.h>

#include "Core/Fs/FileOperations.h"

#include "TempDirectory.h"

#include <fstream>

using Core::CreateEmptyFile;
using Core::Duplicate;
using Core::IsValidFileName;
using Core::MakeDirectory;
using Core::MakeUniquePath;
using Core::MoveToRecycleBin;
using Core::Rename;

TEST_CASE("MakeUniquePath returns the original path when nothing collides")
{
	TempDirectory temp;
	const auto free = temp.GetPath() / "main.cpp";

	CHECK(MakeUniquePath(free) == free);
}

TEST_CASE("MakeUniquePath appends copy on the first collision")
{
	TempDirectory temp;
	temp.MakeFile("main.cpp", "x");

	CHECK(MakeUniquePath(temp.GetPath() / "main.cpp") == temp.GetPath() / "main copy.cpp");
}

TEST_CASE("MakeUniquePath numbers further collisions")
{
	TempDirectory temp;
	temp.MakeFile("main.cpp", "x");
	temp.MakeFile("main copy.cpp", "x");
	temp.MakeFile("main copy 2.cpp", "x");

	CHECK(MakeUniquePath(temp.GetPath() / "main.cpp") == temp.GetPath() / "main copy 3.cpp");
}

TEST_CASE("MakeUniquePath handles a name without an extension")
{
	TempDirectory temp;
	temp.MakeDir("build");

	CHECK(MakeUniquePath(temp.GetPath() / "build") == temp.GetPath() / "build copy");
}

TEST_CASE("MakeUniquePath treats a dotfile as a name, not an extension")
{
	TempDirectory temp;
	temp.MakeFile(".gitignore", "x");

	CHECK(MakeUniquePath(temp.GetPath() / ".gitignore") == temp.GetPath() / ".gitignore copy");
}

TEST_CASE("MakeUniquePath splits a compound extension the way std::filesystem does")
{
	TempDirectory temp;
	temp.MakeFile("archive.tar.gz", "x");

	// stem() is "archive.tar", extension() is ".gz" — consistency with the standard
	// library beats intuition about double extensions.
	CHECK(MakeUniquePath(temp.GetPath() / "archive.tar.gz") == temp.GetPath() / "archive.tar copy.gz");
}

TEST_CASE("IsValidFileName accepts ordinary names")
{
	CHECK(IsValidFileName("main.cpp"));
	CHECK(IsValidFileName(".gitignore"));
	CHECK(IsValidFileName("a name with spaces.txt"));
	CHECK(IsValidFileName("CONSOLE.txt"));
	CHECK(IsValidFileName("COM10.txt"));
}

TEST_CASE("IsValidFileName rejects an empty or blank name")
{
	CHECK_FALSE(IsValidFileName(""));
	CHECK_FALSE(IsValidFileName("   "));
}

TEST_CASE("IsValidFileName rejects characters the filesystem forbids")
{
	CHECK_FALSE(IsValidFileName("a/b"));
	CHECK_FALSE(IsValidFileName("a\\b"));
	CHECK_FALSE(IsValidFileName("a:b"));
	CHECK_FALSE(IsValidFileName("a*b"));
	CHECK_FALSE(IsValidFileName("a?b"));
	CHECK_FALSE(IsValidFileName("a\"b"));
	CHECK_FALSE(IsValidFileName("a<b"));
	CHECK_FALSE(IsValidFileName("a>b"));
	CHECK_FALSE(IsValidFileName("a|b"));
}

TEST_CASE("IsValidFileName rejects a trailing dot or space")
{
	CHECK_FALSE(IsValidFileName("name."));
	CHECK_FALSE(IsValidFileName("name "));
}

TEST_CASE("IsValidFileName rejects reserved device names, with or without an extension")
{
	CHECK_FALSE(IsValidFileName("CON"));
	CHECK_FALSE(IsValidFileName("con"));
	CHECK_FALSE(IsValidFileName("NUL.txt"));
	CHECK_FALSE(IsValidFileName("PRN"));
	CHECK_FALSE(IsValidFileName("AUX"));
	CHECK_FALSE(IsValidFileName("COM1"));
	CHECK_FALSE(IsValidFileName("COM9.cpp"));
	CHECK_FALSE(IsValidFileName("LPT1"));
	CHECK_FALSE(IsValidFileName("lpt9.log"));
}

TEST_CASE("CreateEmptyFile creates a file and refuses to overwrite one")
{
	TempDirectory temp;
	const auto target = temp.GetPath() / "fresh.txt";

	REQUIRE(CreateEmptyFile(target).HasValue());
	CHECK(std::filesystem::exists(target));

	const auto again = CreateEmptyFile(target);
	REQUIRE_FALSE(again.HasValue());
	CHECK(again.GetError().code == Core::ErrorCode::InvalidArgument);
}

TEST_CASE("MakeDirectory creates a directory and refuses an occupied name")
{
	TempDirectory temp;
	const auto target = temp.GetPath() / "sub";

	REQUIRE(MakeDirectory(target).HasValue());
	CHECK(std::filesystem::is_directory(target));

	const auto again = MakeDirectory(target);
	REQUIRE_FALSE(again.HasValue());
	CHECK(again.GetError().code == Core::ErrorCode::InvalidArgument);
}

TEST_CASE("Rename moves a file to the new name")
{
	TempDirectory temp;
	const auto from = temp.MakeFile("before.txt", "payload");
	const auto to = temp.GetPath() / "after.txt";

	REQUIRE(Rename(from, to).HasValue());

	CHECK_FALSE(std::filesystem::exists(from));
	REQUIRE(std::filesystem::exists(to));
}

TEST_CASE("Rename refuses to clobber an existing target")
{
	TempDirectory temp;
	const auto from = temp.MakeFile("before.txt", "a");
	const auto to = temp.MakeFile("taken.txt", "b");

	const auto renamed = Rename(from, to);

	REQUIRE_FALSE(renamed.HasValue());
	CHECK(renamed.GetError().code == Core::ErrorCode::InvalidArgument);
	CHECK(std::filesystem::exists(from));
}

TEST_CASE("Rename reports a missing source")
{
	TempDirectory temp;

	const auto renamed = Rename(temp.GetPath() / "ghost.txt", temp.GetPath() / "any.txt");

	REQUIRE_FALSE(renamed.HasValue());
	CHECK(renamed.GetError().code == Core::ErrorCode::NotFound);
}

TEST_CASE("Rename reports a missing source through the OS error")
{
	TempDirectory temp;

	const auto renamed = Rename(temp.GetPath() / "ghost.txt", temp.GetPath() / "target.txt");

	REQUIRE_FALSE(renamed.HasValue());
	CHECK(renamed.GetError().code == Core::ErrorCode::NotFound);
	CHECK_FALSE(std::filesystem::exists(temp.GetPath() / "target.txt"));
}

TEST_CASE("Duplicate copies a file beside the original and returns the new path")
{
	TempDirectory temp;
	const auto original = temp.MakeFile("main.cpp", "content");

	const auto copied = Duplicate(original);

	REQUIRE(copied.HasValue());
	CHECK(copied.GetValue() == temp.GetPath() / "main copy.cpp");
	REQUIRE(std::filesystem::exists(copied.GetValue()));

	std::ifstream stream{copied.GetValue(), std::ios::binary};
	const std::string contents{std::istreambuf_iterator<char>{stream},
							   std::istreambuf_iterator<char>{}};
	CHECK(contents == "content");
}

TEST_CASE("Duplicate copies a directory recursively")
{
	TempDirectory temp;
	temp.MakeFile("sub/inner.txt", "x");

	const auto copied = Duplicate(temp.GetPath() / "sub");

	REQUIRE(copied.HasValue());
	CHECK(copied.GetValue() == temp.GetPath() / "sub copy");
	CHECK(std::filesystem::exists(copied.GetValue() / "inner.txt"));
}

TEST_CASE("Duplicate reports a missing source")
{
	TempDirectory temp;

	const auto copied = Duplicate(temp.GetPath() / "ghost.txt");

	REQUIRE_FALSE(copied.HasValue());
	CHECK(copied.GetError().code == Core::ErrorCode::NotFound);
}

TEST_CASE("MoveToRecycleBin removes a file from its path")
{
	TempDirectory temp;
	const auto doomed = temp.MakeFile("doomed.txt", "x");

	REQUIRE(MoveToRecycleBin(doomed).HasValue());

	// Where it went is the Recycle Bin's business; that it left this path is ours.
	CHECK_FALSE(std::filesystem::exists(doomed));
}

TEST_CASE("MoveToRecycleBin reports a missing path")
{
	TempDirectory temp;

	const auto removed = MoveToRecycleBin(temp.GetPath() / "ghost.txt");

	REQUIRE_FALSE(removed.HasValue());
	CHECK(removed.GetError().code == Core::ErrorCode::NotFound);
}
