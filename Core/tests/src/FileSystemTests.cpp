#include <doctest/doctest.h>

#include "Core/Fs/FileSystem.h"

#include "TempDirectory.h"

#include <algorithm>
#include <fstream>
#include <string>

using core::ErrorCode;
using core::fs::DirectoryEntry;
using core::fs::NormalizeDirectory;
using core::fs::RelativeTo;
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

TEST_CASE("ReadDirectory returns UTF-8 names for non-ASCII filenames")
{
    TempDirectory temp;
    const std::filesystem::path expected =
        temp.Path() / core::fs::FromUtf8("\xc4\x86wiczenia-\xe6\x97\xa5.txt");

    std::ofstream stream{expected, std::ios::binary | std::ios::trunc};
    stream << "x";
    stream.close();

    auto result = ReadDirectory(temp.Path());

    REQUIRE(result.HasValue());
    REQUIRE(result.Value().size() == 1);
    CHECK(result.Value()[0].name == "\xc4\x86wiczenia-\xe6\x97\xa5.txt");
    CHECK(core::fs::ToUtf8(result.Value()[0].path.filename()) ==
          "\xc4\x86wiczenia-\xe6\x97\xa5.txt");
}

TEST_CASE("NormalizeDirectory strips a trailing separator")
{
    // What tab completion in cmd and PowerShell produces. Without this the workspace
    // root would never compare equal to the same directory reported by the watcher.
    const std::filesystem::path expected{"D:\\work\\project"};

    CHECK(NormalizeDirectory("D:/work/project/") == expected);
    CHECK(NormalizeDirectory("D:\\work\\project\\") == expected);
}

TEST_CASE("NormalizeDirectory leaves a path without a trailing separator alone")
{
    CHECK(NormalizeDirectory("D:/work/project")
          == std::filesystem::path{"D:\\work\\project"});
}

TEST_CASE("NormalizeDirectory keeps a drive root intact")
{
    // Stripping here would yield `D:`, which names whatever directory that drive's
    // cursor points at rather than its root.
    const std::filesystem::path expected{"D:\\"};

    CHECK(NormalizeDirectory("D:/") == expected);
    CHECK(NormalizeDirectory("D:\\") == expected);
}

TEST_CASE("NormalizeDirectory converts separators and resolves dot segments")
{
    CHECK(NormalizeDirectory("D:/work/./project/../project")
          == std::filesystem::path{"D:\\work\\project"});
}

TEST_CASE("A normalized path compares equal to one built by appending")
{
    // The property the whole function exists for: a node path and a path assembled from
    // the same root must match, or a refresh silently reaches nothing.
    const std::filesystem::path root = NormalizeDirectory("D:/work/project/");

    CHECK((root / "file.txt").parent_path() == root);
}

TEST_CASE("RelativeTo spells a path below the base relative to it")
{
    const auto relative = RelativeTo("D:/work/project/src/main.cpp", "D:/work/project");

    REQUIRE(relative.has_value());
    CHECK(*relative == std::filesystem::path{"src"} / "main.cpp");
}

TEST_CASE("RelativeTo refuses a path that is not below the base")
{
    CHECK_FALSE(RelativeTo("D:/elsewhere/main.cpp", "D:/work/project").has_value());
}

TEST_CASE("RelativeTo refuses a path on another drive")
{
    // lexically_relative cannot express this at all and gives an empty path, which is a
    // different failure from ".." and must not read as success.
    CHECK_FALSE(RelativeTo("E:/work/main.cpp", "D:/work").has_value());
}

TEST_CASE("RelativeTo refuses an empty base")
{
    CHECK_FALSE(RelativeTo("D:/work/main.cpp", "").has_value());
}

TEST_CASE("RelativeTo gives \".\" for a path equal to the base")
{
    const auto relative = RelativeTo("D:/work/project", "D:/work/project");

    REQUIRE(relative.has_value());
    CHECK(*relative == ".");
}
