#include <doctest/doctest.h>

#include "Core/Fs/FileSystem.h"

#include "TempDirectory.h"

#include <algorithm>
#include <fstream>
#include <string>

using Core::DirectoryEntry;
using Core::ErrorCode;
using Core::NormalizeDirectory;
using Core::ReadDirectory;
using Core::RelativeTo;

namespace {
	const DirectoryEntry* FindEntry(const std::vector<DirectoryEntry>& entries, const std::string& name)
	{
		const auto it = std::find_if(entries.begin(), entries.end(),
									 [&name](const DirectoryEntry& entry) { return entry.Name == name; });
		return it == entries.end() ? nullptr : &*it;
	}
}

TEST_CASE("ReadDirectory lists files and directories")
{
	TempDirectory temp;
	temp.MakeFile("alpha.txt", "hello");
	temp.MakeDir("beta");

	auto result = ReadDirectory(temp.GetPath());

	REQUIRE(result.HasValue());
	const auto& entries = result.GetValue();
	CHECK(entries.size() == 2);

	const DirectoryEntry* file = FindEntry(entries, "alpha.txt");
	REQUIRE(file != nullptr);
	CHECK_FALSE(file->IsDirectory);
	CHECK(file->Size == 5);

	const DirectoryEntry* directory = FindEntry(entries, "beta");
	REQUIRE(directory != nullptr);
	CHECK(directory->IsDirectory);
}

TEST_CASE("ReadDirectory reports a missing directory as NotFound")
{
	TempDirectory temp;

	auto result = ReadDirectory(temp.GetPath() / "does-not-exist");

	REQUIRE_FALSE(result.HasValue());
	CHECK(result.GetError().Code == ErrorCode::NotFound);
}

TEST_CASE("ReadDirectory returns an empty list for an empty directory")
{
	TempDirectory temp;

	auto result = ReadDirectory(temp.GetPath());

	REQUIRE(result.HasValue());
	CHECK(result.GetValue().empty());
}

TEST_CASE("ReadDirectory fills the full path of every entry")
{
	TempDirectory temp;
	const auto expected = temp.MakeFile("nested/inner.txt", "x");

	auto result = ReadDirectory(temp.GetPath() / "nested");

	REQUIRE(result.HasValue());
	REQUIRE(result.GetValue().size() == 1);
	CHECK(result.GetValue()[0].Path == expected);
}

TEST_CASE("ReadDirectory reports a file path as an error, not an empty listing")
{
	TempDirectory temp;
	const auto file = temp.MakeFile("not-a-directory.txt", "x");

	auto result = ReadDirectory(file);

	REQUIRE_FALSE(result.HasValue());
	CHECK(result.GetError().Code != ErrorCode::None);
}

TEST_CASE("ReadDirectory returns UTF-8 names for non-ASCII filenames")
{
	TempDirectory temp;
	const std::filesystem::path expected =
		temp.GetPath() / Core::FromUtf8("\xc4\x86wiczenia-\xe6\x97\xa5.txt");

	std::ofstream stream{expected, std::ios::binary | std::ios::trunc};
	stream << "x";
	stream.close();

	auto result = ReadDirectory(temp.GetPath());

	REQUIRE(result.HasValue());
	REQUIRE(result.GetValue().size() == 1);
	CHECK(result.GetValue()[0].Name == "\xc4\x86wiczenia-\xe6\x97\xa5.txt");
	CHECK(Core::ToUtf8(result.GetValue()[0].Path.filename()) ==
		  "\xc4\x86wiczenia-\xe6\x97\xa5.txt");
}

// Text from outside — a percent-decoded URI from a language server, a hand-edited settings
// value — is not always valid UTF-8. The conversion is used everywhere as one that cannot fail,
// so the bytes that do not decode become U+FFFD rather than a std::system_error.
TEST_CASE("FromUtf8 turns bytes that are not valid UTF-8 into replacement characters instead of throwing")
{
	std::filesystem::path path;
	REQUIRE_NOTHROW(path = Core::FromUtf8("caf\xe9.txt"));
	CHECK(path.wstring() == L"caf\uFFFD.txt");
}

#ifdef CORE_PLATFORM_WINDOWS
// NTFS allows a name holding a lone surrogate, so ReadDirectory can hand one back.
TEST_CASE("ToUtf8 turns a lone surrogate into a replacement character instead of throwing")
{
	const std::filesystem::path path{std::wstring{L"a"} + wchar_t{0xD800} + L"b.txt"};

	std::string text;
	REQUIRE_NOTHROW(text = Core::ToUtf8(path));
	CHECK(text == "a\xef\xbf\xbd" "b.txt");
}
#endif

TEST_CASE("ToUtf8 and FromUtf8 round-trip a non-ASCII path exactly")
{
	const std::string text = "C:\\\xc4\x86wiczenia\\\xe6\x97\xa5.txt";
	CHECK(Core::ToUtf8(Core::FromUtf8(text)) == text);
	CHECK(Core::ToUtf8(Core::FromUtf8("")).empty());
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
	CHECK(NormalizeDirectory("D:/work/project") == std::filesystem::path{"D:\\work\\project"});
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
	CHECK(NormalizeDirectory("D:/work/./project/../project") == std::filesystem::path{"D:\\work\\project"});
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

TEST_CASE("IsSameOrWithin accepts the root itself and anything below it")
{
	CHECK(Core::IsSameOrWithin("D:\\Repos\\IDE", "D:\\Repos\\IDE"));
	CHECK(Core::IsSameOrWithin("D:\\Repos\\IDE\\Core\\src\\a.cpp", "D:\\Repos\\IDE"));
	CHECK_FALSE(Core::IsSameOrWithin("D:\\Repos", "D:\\Repos\\IDE"));
	CHECK_FALSE(Core::IsSameOrWithin("E:\\Repos\\IDE", "D:\\Repos\\IDE"));
}

TEST_CASE("IsSameOrWithin ignores case, slash direction and a trailing separator")
{
	CHECK(Core::IsSameOrWithin("d:/repos/ide/core/", "D:\\Repos\\IDE\\"));
	CHECK(Core::IsSameOrWithin("D:\\REPOS\\IDE\\x.cpp", "d:\\repos\\ide"));
}

// The prefix trap, the same one WorkspaceTrust guards against.
TEST_CASE("IsSameOrWithin does not treat a sibling sharing a prefix as inside")
{
	CHECK_FALSE(Core::IsSameOrWithin("D:\\Repos\\IDE2\\a.cpp", "D:\\Repos\\IDE"));
}

TEST_CASE("IsSameOrWithin is false for an empty root or a relative path")
{
	CHECK_FALSE(Core::IsSameOrWithin("D:\\Repos\\IDE", ""));
	CHECK_FALSE(Core::IsSameOrWithin("Repos\\IDE", "D:\\Repos"));
	CHECK_FALSE(Core::IsSameOrWithin("D:\\Repos\\IDE", "Repos"));
}

TEST_CASE("IsSameOrWithin treats a drive root as containing the whole drive")
{
	CHECK(Core::IsSameOrWithin("D:\\anything\\at\\all", "D:\\"));
}

TEST_CASE("ReadFileBytes returns the bytes exactly as they sit on disk")
{
	TempDirectory temp;
	const std::string content("a\r\nb\0c", 6);
	temp.MakeFile("data.bin", content);

	const auto result = Core::ReadFileBytes(temp.GetPath() / "data.bin");

	REQUIRE(result.HasValue());
	CHECK(result.GetValue() == content);
}

TEST_CASE("ReadFileBytes reports a missing file as NotFound")
{
	TempDirectory temp;

	const auto result = Core::ReadFileBytes(temp.GetPath() / "missing.txt");

	REQUIRE_FALSE(result.HasValue());
	CHECK(result.GetError().Code == ErrorCode::NotFound);
}

TEST_CASE("ReadFileBytes refuses a file over the limit and accepts one exactly at it")
{
	TempDirectory temp;
	temp.MakeFile("ten.txt", "0123456789");

	const auto over = Core::ReadFileBytes(temp.GetPath() / "ten.txt", 9);
	const auto exact = Core::ReadFileBytes(temp.GetPath() / "ten.txt", 10);

	REQUIRE_FALSE(over.HasValue());
	CHECK(over.GetError().Code == ErrorCode::TooLarge);
	REQUIRE(exact.HasValue());
	CHECK(exact.GetValue() == "0123456789");
}

TEST_CASE("ReadFileBytes reads an empty file as an empty string")
{
	TempDirectory temp;
	temp.MakeFile("empty.txt", "");

	const auto result = Core::ReadFileBytes(temp.GetPath() / "empty.txt", 0);

	REQUIRE(result.HasValue());
	CHECK(result.GetValue().empty());
}
