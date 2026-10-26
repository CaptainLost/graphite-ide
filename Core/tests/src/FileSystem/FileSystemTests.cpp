#include <doctest/doctest.h>

#include "Core/FileSystem/FileSystem.h"

#include "TempDirectory.h"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <system_error>
#include <type_traits>
#include <vector>

#ifdef CORE_PLATFORM_WINDOWS
#include <windows.h>

#include <winioctl.h>
#endif

#include <cstddef>

using Core::DirectoryEntry;
using Core::ErrorCode;
using Core::NormalizeDirectory;
using Core::ReadDirectory;
using Core::RelativeTo;

namespace {
	const DirectoryEntry* FindEntry(const std::vector<DirectoryEntry>& entries, const std::string& name)
	{
		for (const DirectoryEntry& entry : entries)
		{
			if (entry.Name == name)
			{
				return &entry;
			}
		}

		return nullptr;
	}
}

TEST_CASE("ReadDirectory lists files and directories")
{
	TempDirectory temporaryDirectory;
	temporaryDirectory.MakeFile("alpha.txt", "hello");
	temporaryDirectory.MakeDirectory("beta");

	const Core::Result<std::vector<DirectoryEntry>> listing = ReadDirectory(temporaryDirectory.GetPath());

	REQUIRE(listing.HasValue());
	const std::vector<DirectoryEntry>& entries = listing.GetValue();
	CHECK(entries.size() == 2);

	const DirectoryEntry* fileEntry = FindEntry(entries, "alpha.txt");
	REQUIRE(fileEntry != nullptr);
	CHECK_FALSE(fileEntry->IsDirectory);
	CHECK(fileEntry->Size == 5);

	const DirectoryEntry* directoryEntry = FindEntry(entries, "beta");
	REQUIRE(directoryEntry != nullptr);
	CHECK(directoryEntry->IsDirectory);
}

TEST_CASE("ReadDirectory reports a missing directory as NotFound")
{
	TempDirectory temporaryDirectory;

	const Core::Result<std::vector<DirectoryEntry>> listing = ReadDirectory(temporaryDirectory.GetPath() / "does-not-exist");

	REQUIRE_FALSE(listing.HasValue());
	CHECK(listing.GetError().Code == ErrorCode::NotFound);
}

TEST_CASE("ReadDirectory returns an empty list for an empty directory")
{
	TempDirectory temporaryDirectory;

	const Core::Result<std::vector<DirectoryEntry>> listing = ReadDirectory(temporaryDirectory.GetPath());

	REQUIRE(listing.HasValue());
	CHECK(listing.GetValue().empty());
}

TEST_CASE("ReadDirectory fills the full path of every entry")
{
	TempDirectory temporaryDirectory;
	const std::filesystem::path expected = temporaryDirectory.MakeFile("nested/inner.txt", "x");

	const Core::Result<std::vector<DirectoryEntry>> listing = ReadDirectory(temporaryDirectory.GetPath() / "nested");

	REQUIRE(listing.HasValue());
	REQUIRE(listing.GetValue().size() == 1);
	CHECK(listing.GetValue()[0].Path == expected);
}

TEST_CASE("ReadDirectory reports a file path as an error, not an empty listing")
{
	TempDirectory temporaryDirectory;
	const std::filesystem::path file = temporaryDirectory.MakeFile("not-a-directory.txt", "x");

	const Core::Result<std::vector<DirectoryEntry>> listing = ReadDirectory(file);

	REQUIRE_FALSE(listing.HasValue());
	CHECK(listing.GetError().Code != ErrorCode::None);
}

TEST_CASE("ReadDirectory returns UTF-8 names for non-ASCII filenames")
{
	TempDirectory temporaryDirectory;
	const std::filesystem::path expected =
		temporaryDirectory.GetPath() / Core::FromUtf8("\xc4\x86wiczenia-\xe6\x97\xa5.txt");

	std::ofstream stream{expected, std::ios::binary | std::ios::trunc};
	stream << "x";
	stream.close();

	const Core::Result<std::vector<DirectoryEntry>> listing = ReadDirectory(temporaryDirectory.GetPath());

	REQUIRE(listing.HasValue());
	REQUIRE(listing.GetValue().size() == 1);
	CHECK(listing.GetValue()[0].Name == "\xc4\x86wiczenia-\xe6\x97\xa5.txt");
	CHECK(Core::ToUtf8(listing.GetValue()[0].Path.filename()) ==
		  "\xc4\x86wiczenia-\xe6\x97\xa5.txt");
}

#ifdef CORE_PLATFORM_WINDOWS
TEST_CASE("ReadDirectory counts a file with the system attribute as hidden as well as one with the hidden attribute")
{
	TempDirectory temporaryDirectory;
	const std::filesystem::path hiddenFile = temporaryDirectory.MakeFile("hidden.txt", "x");
	const std::filesystem::path systemFile = temporaryDirectory.MakeFile("system.txt", "x");
	temporaryDirectory.MakeFile("plain.txt", "x");
	REQUIRE(::SetFileAttributesW(hiddenFile.c_str(), FILE_ATTRIBUTE_HIDDEN) != FALSE);
	REQUIRE(::SetFileAttributesW(systemFile.c_str(), FILE_ATTRIBUTE_SYSTEM) != FALSE);

	const Core::Result<std::vector<DirectoryEntry>> listing = ReadDirectory(temporaryDirectory.GetPath());
	REQUIRE(listing.HasValue());

	const DirectoryEntry* hiddenEntry = FindEntry(listing.GetValue(), "hidden.txt");
	const DirectoryEntry* systemEntry = FindEntry(listing.GetValue(), "system.txt");
	const DirectoryEntry* plainEntry = FindEntry(listing.GetValue(), "plain.txt");
	REQUIRE(hiddenEntry != nullptr);
	REQUIRE(systemEntry != nullptr);
	REQUIRE(plainEntry != nullptr);
	CHECK(hiddenEntry->IsHidden);
	CHECK(systemEntry->IsHidden);
	CHECK_FALSE(plainEntry->IsHidden);
}

namespace {
	// What the listing was before it read the attributes from the enumeration: the standard walk, and one attribute
	// query per entry.
	std::vector<DirectoryEntry> ListedEntryByEntry(const std::filesystem::path& folder)
	{
		std::vector<DirectoryEntry> entries;

		for (const std::filesystem::directory_entry& walked : std::filesystem::directory_iterator{folder})
		{
			const DWORD attributes = ::GetFileAttributesW(walked.path().c_str());
			std::error_code ignoredClassifyError;
			const bool isDirectory = walked.is_directory(ignoredClassifyError);
			std::error_code sizeError;
			const std::uintmax_t fileSize = isDirectory ? 0 : walked.file_size(sizeError);

			entries.push_back(DirectoryEntry{
				.Path = walked.path(),
				.Name = Core::ToUtf8(walked.path().filename()),
				.IsDirectory = isDirectory,
				.IsHidden = (attributes & (FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM)) != 0,
				.Size = sizeError ? 0 : fileSize,
			});
		}

		return entries;
	}

	void MarkHidden(const std::filesystem::path& path)
	{
		REQUIRE(::SetFileAttributesW(path.c_str(), FILE_ATTRIBUTE_HIDDEN) != FALSE);
	}
}

TEST_CASE("ReadDirectory lists the same entries, in the same order and with the same flags, as a walk that asks for each entry's attributes")
{
	TempDirectory temporaryDirectory;
	temporaryDirectory.MakeFile("zeta.txt", "twelve bytes");
	temporaryDirectory.MakeFile("Alpha.cpp", "x");
	temporaryDirectory.MakeFile("middle", "");
	temporaryDirectory.MakeDirectory("beta");
	temporaryDirectory.MakeDirectory(".config");
	MarkHidden(temporaryDirectory.MakeFile(".hidden-file", "secret"));
	MarkHidden(temporaryDirectory.MakeDirectory("hidden-folder"));
	temporaryDirectory.MakeFile("beta/inside.txt", "not listed");

	const Core::Result<std::vector<DirectoryEntry>> listing = ReadDirectory(temporaryDirectory.GetPath());
	REQUIRE(listing.HasValue());

	const std::vector<DirectoryEntry> expected = ListedEntryByEntry(temporaryDirectory.GetPath());
	const std::vector<DirectoryEntry>& listed = listing.GetValue();

	REQUIRE(listed.size() == expected.size());

	for (std::size_t entryIndex = 0; entryIndex < listed.size(); ++entryIndex)
	{
		CAPTURE(expected[entryIndex].Name);
		CHECK(listed[entryIndex].Path == expected[entryIndex].Path);
		CHECK(listed[entryIndex].Name == expected[entryIndex].Name);
		CHECK(listed[entryIndex].IsDirectory == expected[entryIndex].IsDirectory);
		CHECK(listed[entryIndex].IsHidden == expected[entryIndex].IsHidden);
		CHECK(listed[entryIndex].Size == expected[entryIndex].Size);
	}

	const DirectoryEntry* hiddenFolder = FindEntry(listed, "hidden-folder");
	REQUIRE(hiddenFolder != nullptr);
	CHECK(hiddenFolder->IsHidden);
	CHECK(hiddenFolder->IsDirectory);
}

TEST_CASE("ReadDirectory lists a link as what it points at, and a dangling link as an empty file")
{
	TempDirectory temporaryDirectory;
	temporaryDirectory.MakeFile("target.txt", "twelve bytes");
	temporaryDirectory.MakeFile("target-folder/inside.txt", "x");

	const std::filesystem::path links = temporaryDirectory.MakeDirectory("links");
	std::error_code linkError;
	std::filesystem::create_symlink(temporaryDirectory.GetPath() / "target.txt", links / "file-link.txt", linkError);

	if (linkError)
	{
		MESSAGE("Skipped: this process cannot create symbolic links: " << linkError.message());
		return;
	}

	std::filesystem::create_directory_symlink(temporaryDirectory.GetPath() / "target-folder", links / "folder-link", linkError);
	REQUIRE_FALSE(linkError);
	std::filesystem::create_directory_symlink(temporaryDirectory.GetPath() / "gone", links / "dangling-link", linkError);
	REQUIRE_FALSE(linkError);

	const Core::Result<std::vector<DirectoryEntry>> listing = ReadDirectory(links);
	REQUIRE(listing.HasValue());

	const std::vector<DirectoryEntry> expected = ListedEntryByEntry(links);
	REQUIRE(listing.GetValue().size() == expected.size());

	for (std::size_t entryIndex = 0; entryIndex < expected.size(); ++entryIndex)
	{
		CAPTURE(expected[entryIndex].Name);
		CHECK(listing.GetValue()[entryIndex].Name == expected[entryIndex].Name);
		CHECK(listing.GetValue()[entryIndex].IsDirectory == expected[entryIndex].IsDirectory);
		CHECK(listing.GetValue()[entryIndex].Size == expected[entryIndex].Size);
	}

	const DirectoryEntry* fileLink = FindEntry(listing.GetValue(), "file-link.txt");
	const DirectoryEntry* folderLink = FindEntry(listing.GetValue(), "folder-link");
	const DirectoryEntry* danglingLink = FindEntry(listing.GetValue(), "dangling-link");
	REQUIRE(fileLink != nullptr);
	REQUIRE(folderLink != nullptr);
	REQUIRE(danglingLink != nullptr);

	CHECK_FALSE(fileLink->IsDirectory);
	CHECK(fileLink->Size == 12);
	CHECK(folderLink->IsDirectory);
	CHECK_FALSE(danglingLink->IsDirectory);
	CHECK(danglingLink->Size == 0);
}

namespace {
	constexpr std::size_t kReparseHeaderBytes = 8;
	constexpr std::size_t kMountPointNameFieldBytes = 8;

	void AppendBytes(std::vector<std::byte>& buffer, const void* data, std::size_t byteCount)
	{
		const std::byte* first = static_cast<const std::byte*>(data);
		buffer.insert(buffer.end(), first, first + byteCount);
	}

	void AppendWord(std::vector<std::byte>& buffer, std::uint16_t word)
	{
		AppendBytes(buffer, &word, sizeof(word));
	}

	// WARNING: The reparse buffer's layout is the kernel's, and no user-mode header declares it.
	bool MakeJunction(const std::filesystem::path& junction, const std::filesystem::path& target)
	{
		if (::CreateDirectoryW(junction.c_str(), nullptr) == FALSE)
		{
			return false;
		}

		const std::wstring substituteName = L"\\??\\" + target.wstring();
		const std::wstring printName = target.wstring();
		const std::size_t namesBytes = (substituteName.size() + 1 + printName.size() + 1) * sizeof(wchar_t);

		std::vector<std::byte> buffer;
		const DWORD tag = IO_REPARSE_TAG_MOUNT_POINT;
		AppendBytes(buffer, &tag, sizeof(tag));
		AppendWord(buffer, static_cast<std::uint16_t>(kMountPointNameFieldBytes + namesBytes));
		AppendWord(buffer, 0);
		AppendWord(buffer, 0);
		AppendWord(buffer, static_cast<std::uint16_t>(substituteName.size() * sizeof(wchar_t)));
		AppendWord(buffer, static_cast<std::uint16_t>((substituteName.size() + 1) * sizeof(wchar_t)));
		AppendWord(buffer, static_cast<std::uint16_t>(printName.size() * sizeof(wchar_t)));
		AppendBytes(buffer, substituteName.c_str(), (substituteName.size() + 1) * sizeof(wchar_t));
		AppendBytes(buffer, printName.c_str(), (printName.size() + 1) * sizeof(wchar_t));

		const HANDLE handle = ::CreateFileW(junction.c_str(), GENERIC_WRITE, 0, nullptr, OPEN_EXISTING,
											FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);

		if (handle == INVALID_HANDLE_VALUE)
		{
			return false;
		}

		DWORD bytesReturned = 0;
		const BOOL set = ::DeviceIoControl(handle, FSCTL_SET_REPARSE_POINT, buffer.data(), static_cast<DWORD>(buffer.size()),
										   nullptr, 0, &bytesReturned, nullptr);
		::CloseHandle(handle);

		return set != FALSE && buffer.size() == kReparseHeaderBytes + kMountPointNameFieldBytes + namesBytes;
	}
}

TEST_CASE("ReadDirectory lists a junction as the folder it points at, and a dangling one as an empty file")
{
	TempDirectory temporaryDirectory;
	temporaryDirectory.MakeFile("target-folder/inside.txt", "x");
	const std::filesystem::path links = temporaryDirectory.MakeDirectory("links");

	REQUIRE(MakeJunction(links / "folder-junction", temporaryDirectory.GetPath() / "target-folder"));
	REQUIRE(MakeJunction(links / "dangling-junction", temporaryDirectory.GetPath() / "gone"));

	const Core::Result<std::vector<DirectoryEntry>> listing = ReadDirectory(links);
	REQUIRE(listing.HasValue());

	const DirectoryEntry* folderJunction = FindEntry(listing.GetValue(), "folder-junction");
	const DirectoryEntry* danglingJunction = FindEntry(listing.GetValue(), "dangling-junction");
	REQUIRE(folderJunction != nullptr);
	REQUIRE(danglingJunction != nullptr);

	CHECK(folderJunction->IsDirectory);
	CHECK_FALSE(danglingJunction->IsDirectory);
	CHECK(danglingJunction->Size == 0);

	std::error_code ignoredRemoveError;
	std::filesystem::remove(links / "folder-junction", ignoredRemoveError);
	std::filesystem::remove(links / "dangling-junction", ignoredRemoveError);
}

TEST_CASE("ReadDirectory of a drive root lists its entries without the dot entries")
{
	const std::filesystem::path systemDrive = std::filesystem::temp_directory_path().root_path();

	const Core::Result<std::vector<DirectoryEntry>> listing = ReadDirectory(systemDrive);

	REQUIRE(listing.HasValue());
	CHECK(FindEntry(listing.GetValue(), ".") == nullptr);
	CHECK(FindEntry(listing.GetValue(), "..") == nullptr);
	CHECK(listing.GetValue().size() == ListedEntryByEntry(systemDrive).size());
}
#endif

TEST_CASE("FromUtf8 turns bytes from outside that are not valid UTF-8 into replacement characters instead of throwing")
{
	std::filesystem::path path;
	REQUIRE_NOTHROW(path = Core::FromUtf8("caf\xe9.txt"));
	CHECK(path.wstring() == L"caf\uFFFD.txt");
}

#ifdef CORE_PLATFORM_WINDOWS
TEST_CASE("ToUtf8 turns a lone surrogate which NTFS allows in a name into a replacement character instead of throwing")
{
	constexpr wchar_t kLoneHighSurrogate = 0xD800;
	const std::filesystem::path path{std::wstring{L"a"} + kLoneHighSurrogate + L"b.txt"};

	std::string text;
	REQUIRE_NOTHROW(text = Core::ToUtf8(path));
	CHECK(text == "a\xef\xbf\xbd"
				  "b.txt");
}
#endif

TEST_CASE("ToUtf8 and FromUtf8 round-trip a non-ASCII path exactly")
{
	const std::string text = "C:\\\xc4\x86wiczenia\\\xe6\x97\xa5.txt";
	CHECK(Core::ToUtf8(Core::FromUtf8(text)) == text);
	CHECK(Core::ToUtf8(Core::FromUtf8("")).empty());
}

TEST_CASE("ToGenericUtf8 writes every separator as a forward slash the way git spells paths")
{
	CHECK(Core::ToGenericUtf8(std::filesystem::path{"D:\\work\\src/main.cpp"}) == "D:/work/src/main.cpp");
}

TEST_CASE("NormalizePath resolves dot segments and spells both separators alike but keeps a trailing separator")
{
	CHECK(Core::ToUtf8(Core::NormalizePath("D:/work/./src/../main.cpp")) == "D:\\work\\main.cpp");
	CHECK(Core::ToUtf8(Core::NormalizePath("D:/work/")) == "D:\\work\\");
}

TEST_CASE("NormalizeDirectory strips the trailing separator tab completion in cmd and PowerShell leaves so the root equals the directory the watcher reports")
{
	const std::filesystem::path expected{"D:\\work\\project"};

	CHECK(NormalizeDirectory("D:/work/project/") == expected);
	CHECK(NormalizeDirectory("D:\\work\\project\\") == expected);
}

TEST_CASE("NormalizeDirectory leaves a path without a trailing separator alone")
{
	CHECK(NormalizeDirectory("D:/work/project") == std::filesystem::path{"D:\\work\\project"});
}

TEST_CASE("NormalizeDirectory keeps a drive root intact because D: alone names the current directory on that drive")
{
	const std::filesystem::path expected{"D:\\"};

	CHECK(NormalizeDirectory("D:/") == expected);
	CHECK(NormalizeDirectory("D:\\") == expected);
}

TEST_CASE("NormalizeDirectory converts separators and resolves dot segments")
{
	CHECK(NormalizeDirectory("D:/work/./project/../project") == std::filesystem::path{"D:\\work\\project"});
}

TEST_CASE("A normalized directory equals the parent of a path built by appending to it or a refresh would reach no node")
{
	const std::filesystem::path root = NormalizeDirectory("D:/work/project/");

	CHECK((root / "file.txt").parent_path() == root);
}

TEST_CASE("RelativeTo spells a path below the base relative to it")
{
	const std::optional<std::filesystem::path> relative = RelativeTo("D:/work/project/src/main.cpp", "D:/work/project");

	REQUIRE(relative.has_value());
	CHECK(*relative == std::filesystem::path{"src"} / "main.cpp");
}

TEST_CASE("RelativeTo refuses a path that is not below the base")
{
	CHECK_FALSE(RelativeTo("D:/elsewhere/main.cpp", "D:/work/project").has_value());
}

TEST_CASE("RelativeTo refuses a path on another drive rather than giving an empty path that could read as success")
{
	CHECK_FALSE(RelativeTo("E:/work/main.cpp", "D:/work").has_value());
}

TEST_CASE("RelativeToKey agrees with RelativeTo for a base key made once")
{
	const Core::PathKeyText baseKey = Core::PathKey("D:/work/project");

	const std::optional<std::filesystem::path> inside = Core::RelativeToKey("D:/work/project/src/main.cpp", baseKey);
	REQUIRE(inside.has_value());
	CHECK(*inside == std::filesystem::path{"src"} / "main.cpp");

	CHECK(Core::RelativeToKey("D:/work/project", baseKey) == std::filesystem::path{"."});
	CHECK_FALSE(Core::RelativeToKey("D:/work/project2/a", baseKey).has_value());

	const Core::PathKeyText emptyBaseKey = Core::PathKey("");
	CHECK_FALSE(Core::RelativeToKey("D:/work/project/a", emptyBaseKey).has_value());
}

TEST_CASE("RelativeTo refuses an empty base")
{
	CHECK_FALSE(RelativeTo("D:/work/main.cpp", "").has_value());
}

TEST_CASE("RelativeTo gives \".\" for a path equal to the base")
{
	const std::optional<std::filesystem::path> relative = RelativeTo("D:/work/project", "D:/work/project");

	REQUIRE(relative.has_value());
	CHECK(*relative == ".");
}

TEST_CASE("RelativeTo ignores the case of the base and its drive letter the way Windows does and keeps the case the path was written in")
{
	const std::optional<std::filesystem::path> relative = RelativeTo("d:/Work/Project/src/main.cpp", "D:/work/project/");

	REQUIRE(relative.has_value());
	CHECK(*relative == std::filesystem::path{"src"} / "main.cpp");

	CHECK(RelativeTo("D:/Work/Project/Src/Main.cpp", "d:/work/project") == std::filesystem::path{"Src"} / "Main.cpp");
}

#ifdef CORE_PLATFORM_WINDOWS
TEST_CASE("Every path comparison folds case beyond ASCII the way Windows does so that no two of them disagree about one folder")
{
	const std::filesystem::path fileInUpperCaseFolder{L"D:\\\u0179R\u00d3D\u0141A\\a.txt"};
	const std::filesystem::path lowerCaseFolder{L"d:\\\u017ar\u00f3d\u0142a"};

	CHECK(Core::IsSameOrWithin(fileInUpperCaseFolder, lowerCaseFolder));
	CHECK(RelativeTo(fileInUpperCaseFolder, lowerCaseFolder) == std::filesystem::path{"a.txt"});
	CHECK(Core::SamePath(fileInUpperCaseFolder.parent_path(), lowerCaseFolder));
	CHECK(Core::PathKey(fileInUpperCaseFolder.parent_path()) == Core::PathKey(lowerCaseFolder));
}
#endif

TEST_CASE("PathKey is one value for every spelling of a directory")
{
	CHECK(Core::PathKey("D:\\Repos\\IDE") == Core::PathKey("d:/repos/ide/"));
	CHECK(Core::PathKey("D:\\Repos\\IDE") == Core::PathKey("D:\\Repos\\x\\..\\IDE"));
	CHECK(Core::PathKey("D:\\Repos\\IDE") != Core::PathKey("D:\\Repos\\IDE2"));
	CHECK(Core::PathKey("D:\\") == Core::PathKey("d:/"));
	CHECK(Core::PathKey("").empty());
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
	TempDirectory temporaryDirectory;
	const std::string content("a\r\nb\0c", 6);
	temporaryDirectory.MakeFile("data.bin", content);

	const Core::Result<std::string> fileBytes = Core::ReadFileBytes(temporaryDirectory.GetPath() / "data.bin");

	REQUIRE(fileBytes.HasValue());
	CHECK(fileBytes.GetValue() == content);
}

TEST_CASE("ReadFileBytes reports a missing file as NotFound")
{
	TempDirectory temporaryDirectory;

	const Core::Result<std::string> fileBytes = Core::ReadFileBytes(temporaryDirectory.GetPath() / "missing.txt");

	REQUIRE_FALSE(fileBytes.HasValue());
	CHECK(fileBytes.GetError().Code == ErrorCode::NotFound);
}

TEST_CASE("ReadFileBytes refuses a file over the limit and accepts one exactly at it")
{
	TempDirectory temporaryDirectory;
	temporaryDirectory.MakeFile("ten.txt", "0123456789");

	const std::uintmax_t limitOneBelowSize = 9;
	const std::uintmax_t limitEqualToSize = 10;
	const Core::Result<std::string> overLimit = Core::ReadFileBytes(temporaryDirectory.GetPath() / "ten.txt", limitOneBelowSize);
	const Core::Result<std::string> atLimit = Core::ReadFileBytes(temporaryDirectory.GetPath() / "ten.txt", limitEqualToSize);

	REQUIRE_FALSE(overLimit.HasValue());
	CHECK(overLimit.GetError().Code == ErrorCode::TooLarge);
	REQUIRE(atLimit.HasValue());
	CHECK(atLimit.GetValue() == "0123456789");
}

TEST_CASE("ReadFileBytes reads an empty file as an empty string")
{
	TempDirectory temporaryDirectory;
	temporaryDirectory.MakeFile("empty.txt", "");

	const std::uintmax_t noBytesAllowed = 0;
	const Core::Result<std::string> fileBytes = Core::ReadFileBytes(temporaryDirectory.GetPath() / "empty.txt", noBytesAllowed);

	REQUIRE(fileBytes.HasValue());
	CHECK(fileBytes.GetValue().empty());
}

TEST_CASE("A path separator is either slash, and the leaf is what follows the last one")
{
	static_assert(Core::IsPathSeparator('/'));
	static_assert(Core::IsPathSeparator('\\'));
	static_assert(!Core::IsPathSeparator(':'));
	static_assert(!Core::IsPathSeparator('\xAF'));
	static_assert(!Core::IsPathSeparator('\0'));

	CHECK(Core::AfterLastPathSeparator("a/b\\c") == "c");
	CHECK(Core::AfterLastPathSeparator("c") == "c");
	CHECK(Core::AfterLastPathSeparator("dir/").empty());
	CHECK(Core::AfterLastPathSeparator("").empty());
	CHECK(Core::AfterLastPathSeparator("\\").empty());
}

#ifdef CORE_PLATFORM_WINDOWS
TEST_CASE("ToUtf16 converts every byte it is given and replaces what is not UTF-8")
{
	CHECK(Core::ToUtf16("").empty());
	CHECK(Core::ToUtf16("abc") == L"abc");
	CHECK(Core::ToUtf16(std::string_view{"a\0b", 3}) == std::wstring{L"a\0b", 3});
	CHECK(Core::ToUtf16("\xE6"
						"a") == L"\xFFFD"
								L"a");
	CHECK(Core::ToUtf16("\xF0\x9F\x98\x80") == L"\xD83D\xDE00");

	const std::string_view polishText = "za\xC5\xBC\xC3\xB3\xC5\x82\xC4\x87";
	CHECK(Core::FromUtf8(polishText).native() == Core::ToUtf16(polishText));
}
#endif

TEST_CASE("A path key is the native string type")
{
	static_assert(std::is_same_v<Core::PathKeyText, std::filesystem::path::string_type>);
	static_assert(std::is_same_v<decltype(Core::PathKey(std::filesystem::path{})), Core::PathKeyText>);
}

TEST_CASE("PathExists reads anything it cannot examine as absent")
{
	TempDirectory temporaryDirectory;

	CHECK(Core::PathExists(temporaryDirectory.GetPath()));
	CHECK(Core::PathExists(temporaryDirectory.MakeFile("a.txt")));
	CHECK_FALSE(Core::PathExists(temporaryDirectory.GetPath() / "absent.txt"));
	CHECK_FALSE(Core::PathExists(std::filesystem::path{}));
}

TEST_CASE("LoweredExtension gives the extension with its dot, in lower case")
{
	CHECK(Core::LoweredExtension("D:/work/App.VCXPROJ") == ".vcxproj");
	CHECK(Core::LoweredExtension("Makefile").empty());
	CHECK(Core::LoweredExtension("archive.tar.GZ") == ".gz");
}

TEST_CASE("CompareNames orders names ignoring ASCII case and is negative when the left name comes first")
{
	CHECK(Core::CompareNames("alpha", "Beta") < 0);
	CHECK(Core::CompareNames("Beta", "alpha") > 0);
	CHECK(Core::CompareNames("README.md", "readme.MD") == 0);
}

TEST_CASE("CompareNames puts a name before a longer name it begins")
{
	CHECK(Core::CompareNames("main", "main.cpp") < 0);
	CHECK(Core::CompareNames("main.cpp", "main") > 0);
}
