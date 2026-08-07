#include <doctest/doctest.h>

#include "Core/Fs/FileSystem.h"

#include "TempDirectory.h"

#include <algorithm>
#include <string>

using core::ErrorCode;
using core::fs::DirectoryEntry;
using core::fs::ReadDirectory;

namespace
{
    const DirectoryEntry* FindEntry(const std::vector<DirectoryEntry>& entries, const std::string& name)
    {
        const auto it = std::find_if(entries.begin(), entries.end(),
                                     [&name](const DirectoryEntry& entry) { return entry.name == name; });
        return it == entries.end() ? nullptr : &*it;
    }
}

TEST_CASE("ReadDirectory lists files and directories")
{
    TempDirectory temp;
    temp.MakeFile("alpha.txt", "hello");
    temp.MakeDir("beta");

    auto result = ReadDirectory(temp.Path());

    REQUIRE(result.HasValue());
    const auto& entries = result.Value();
    CHECK(entries.size() == 2);

    const DirectoryEntry* file = FindEntry(entries, "alpha.txt");
    REQUIRE(file != nullptr);
    CHECK_FALSE(file->isDirectory);
    CHECK(file->size == 5);

    const DirectoryEntry* directory = FindEntry(entries, "beta");
    REQUIRE(directory != nullptr);
    CHECK(directory->isDirectory);
}

TEST_CASE("ReadDirectory reports a missing directory as NotFound")
{
    TempDirectory temp;

    auto result = ReadDirectory(temp.Path() / "does-not-exist");

    REQUIRE_FALSE(result.HasValue());
    CHECK(result.GetError().code == ErrorCode::NotFound);
}

TEST_CASE("ReadDirectory returns an empty list for an empty directory")
{
    TempDirectory temp;

    auto result = ReadDirectory(temp.Path());

    REQUIRE(result.HasValue());
    CHECK(result.Value().empty());
}

TEST_CASE("ReadDirectory fills the full path of every entry")
{
    TempDirectory temp;
    const auto expected = temp.MakeFile("nested/inner.txt", "x");

    auto result = ReadDirectory(temp.Path() / "nested");

    REQUIRE(result.HasValue());
    REQUIRE(result.Value().size() == 1);
    CHECK(result.Value()[0].path == expected);
}

TEST_CASE("ReadDirectory reports a file path as an error, not an empty listing")
{
    TempDirectory temp;
    const auto file = temp.MakeFile("not-a-directory.txt", "x");

    auto result = ReadDirectory(file);

    REQUIRE_FALSE(result.HasValue());
    CHECK(result.GetError().code != ErrorCode::None);
}
