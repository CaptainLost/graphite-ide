#include <doctest/doctest.h>

#include "Git/Changes/IgnoreEntry.h"

#include "TempDirectory.h"

#include "Core/FileSystem/FileSystem.h"

TEST_CASE("An ignore line is anchored at the root, uses forward slashes and marks a directory")
{
	CHECK(Graphite::IgnoreLineFor(std::filesystem::path{"build"} / "out.log", false) == "/build/out.log");
	CHECK(Graphite::IgnoreLineFor(std::filesystem::path{"bin"}, true) == "/bin/");
}

TEST_CASE("An ignore line is added on a line of its own in the file's own line endings, and only once")
{
	CHECK(Graphite::WithIgnoreLine("", "/bin/") == "/bin/\n");
	CHECK(Graphite::WithIgnoreLine("*.obj", "/bin/") == "*.obj\n/bin/\n");
	CHECK(Graphite::WithIgnoreLine("*.obj\r\n", "/bin/") == "*.obj\r\n/bin/\r\n");
	CHECK(Graphite::WithIgnoreLine("/bin/\r\n*.obj\r\n", "/bin/") == "/bin/\r\n*.obj\r\n");
}

TEST_CASE("Adding to .gitignore creates the file at the root or extends the one there")
{
	TempDirectory temp;

	REQUIRE(Graphite::AddToGitIgnore(temp.GetPath(), "notes.txt", false).HasValue());
	REQUIRE(Graphite::AddToGitIgnore(temp.GetPath(), "out", true).HasValue());

	const Core::Result<std::string> written = Core::ReadFileBytes(temp.GetPath() / ".gitignore");
	REQUIRE(written.HasValue());
	CHECK(written.GetValue() == "/notes.txt\n/out/\n");
}
