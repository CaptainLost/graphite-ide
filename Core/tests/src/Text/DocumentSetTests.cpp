#include <doctest/doctest.h>

#include "Core/Text/DocumentSet.h"
#include "Core/Text/FileProbe.h"

#include "FileFixtures.h"
#include "SparseFileFixture.h"
#include "TempDirectory.h"

#include <cstdint>
#include <ostream>
#include <string>
#include <string_view>

#ifdef CORE_PLATFORM_WINDOWS
#include <windows.h>
#endif

using namespace Core;

namespace {
	constexpr std::string_view kBinaryFileBytes{"MZ\0\0binary", 10};
	constexpr std::string_view kExecutableHeaderBytes{"MZ\0\0", 4};

	constexpr std::uint64_t kSevenHundredEightyMegabytes = 780ull * 1024 * 1024;

#ifdef CORE_PLATFORM_WINDOWS
	constexpr DWORD kShareWithNoOne = 0;
#endif
}

TEST_CASE("A new set is empty")
{
	const DocumentSet documents;

	CHECK(documents.GetCount() == 0);
	CHECK_FALSE(documents.GetActiveIndex().has_value());
}

TEST_CASE("Opening a text file adds an item holding the document")
{
	TempDirectory directory;
	const std::filesystem::path path = directory.GetPath() / "main.cpp";
	WriteFileBytes(path, "int main() {}\n");

	DocumentSet documents;

	CHECK(documents.Open(path, TabKind::Permanent) == OpenOutcome::Opened);
	REQUIRE(documents.GetCount() == 1);
	REQUIRE(documents.At(0).Document.has_value());
	CHECK(documents.At(0).Document->GetBuffer().Materialize() == "int main() {}\n");
	CHECK(documents.At(0).Failure == OpenFailure::None);
	CHECK(documents.GetActiveIndex() == std::optional<std::size_t>{0});
}

TEST_CASE("The widest line is measured in columns not bytes so a line of nine two-byte characters is narrower than one of eleven ASCII characters")
{
	TempDirectory directory;
	const std::filesystem::path path = directory.GetPath() / "widths.txt";
	WriteFileBytes(path, "\xc4\x85\xc4\x87\xc4\x99\xc5\x82\xc5\x84\xc3\xb3\xc5\x9b\xc5\xba\xc5\xbc\n"
						 "abcdefghijk\n");

	DocumentSet documents;

	CHECK(documents.Open(path, TabKind::Permanent) == OpenOutcome::Opened);
	REQUIRE(documents.GetCount() == 1);
	CHECK(documents.At(0).WidestColumn == 11);
}

TEST_CASE("Indent depths are computed once when the document loads")
{
	TempDirectory directory;
	const std::filesystem::path path = directory.GetPath() / "indent.cpp";
	WriteFileBytes(path, "if (x)\n    return;");

	DocumentSet documents;

	CHECK(documents.Open(path, TabKind::Permanent) == OpenOutcome::Opened);
	REQUIRE(documents.GetCount() == 1);
	REQUIRE(documents.At(0).IndentDepths.size() == 2);
	CHECK(documents.At(0).IndentDepths[0] == 0);
	CHECK(documents.At(0).IndentDepths[1] == 4);
}

TEST_CASE("Opening the same file twice activates it instead of duplicating it")
{
	TempDirectory directory;
	const std::filesystem::path firstFile = directory.GetPath() / "a.txt";
	const std::filesystem::path secondFile = directory.GetPath() / "b.txt";
	WriteFileBytes(firstFile, "a");
	WriteFileBytes(secondFile, "b");

	DocumentSet documents;
	documents.Open(firstFile, TabKind::Permanent);
	documents.Open(secondFile, TabKind::Permanent);
	REQUIRE(documents.GetCount() == 2);
	REQUIRE(documents.GetActiveIndex() == std::optional<std::size_t>{1});

	CHECK(documents.Open(firstFile, TabKind::Permanent) == OpenOutcome::Activated);
	CHECK(documents.GetCount() == 2);
	CHECK(documents.GetActiveIndex() == std::optional<std::size_t>{0});
}

TEST_CASE("A binary file opens as an item with a reason rather than nothing at all")
{
	TempDirectory directory;
	const std::filesystem::path path = directory.GetPath() / "object.obj";
	WriteFileBytes(path, kBinaryFileBytes);

	DocumentSet documents;

	CHECK(documents.Open(path, TabKind::Permanent) == OpenOutcome::Failed);
	REQUIRE(documents.GetCount() == 1);
	CHECK_FALSE(documents.At(0).Document.has_value());
	CHECK(documents.At(0).Failure == OpenFailure::Binary);
	CHECK(FailureMessage(documents.At(0)).find("Binary") != std::string::npos);
}

TEST_CASE("A UTF-16 file opens as a document with its text decoded")
{
	TempDirectory directory;
	const std::filesystem::path path = directory.GetPath() / "utf16.txt";
	const std::string littleEndianHelloAfterByteOrderMark("\xFF\xFEH\0e\0l\0l\0o\0", 12);
	WriteFileBytes(path, littleEndianHelloAfterByteOrderMark);

	DocumentSet documents;

	CHECK(documents.Open(path, TabKind::Permanent) == OpenOutcome::Opened);
	REQUIRE(documents.GetCount() == 1);
	REQUIRE(documents.At(0).Document.has_value());
	CHECK(documents.At(0).Document->GetBuffer().Materialize() == "Hello");
}

TEST_CASE("A damaged UTF-16 file says it is damaged, not that the format is unsupported")
{
	TempDirectory directory;
	const std::filesystem::path path = directory.GetPath() / "damaged.txt";
	const std::string loneLowSurrogateAfterByteOrderMark("\xFF\xFE\x42\xDE", 4);
	WriteFileBytes(path, loneLowSurrogateAfterByteOrderMark);

	DocumentSet documents;

	CHECK(documents.Open(path, TabKind::Permanent) == OpenOutcome::Failed);
	REQUIRE(documents.GetCount() == 1);
	CHECK(documents.At(0).Failure == OpenFailure::ReadFailed);
	CHECK(FailureMessage(documents.At(0)).find("damaged") != std::string::npos);
}

TEST_CASE("A file that is not there opens as an item saying so, not as silence")
{
	TempDirectory directory;

	DocumentSet documents;

	CHECK(documents.Open(directory.GetPath() / "absent.txt", TabKind::Permanent) == OpenOutcome::Failed);
	REQUIRE(documents.GetCount() == 1);
	CHECK(documents.At(0).Failure == OpenFailure::Missing);
}

TEST_CASE("Opening a directory does not claim it does not exist")
{
	TempDirectory directory;
	const std::filesystem::path subfolder = directory.MakeDirectory("subfolder");

	DocumentSet documents;

	CHECK(documents.Open(subfolder, TabKind::Permanent) == OpenOutcome::Failed);
	REQUIRE(documents.GetCount() == 1);
	CHECK(documents.At(0).Failure == OpenFailure::ReadFailed);
	CHECK(documents.At(0).Failure != OpenFailure::Missing);
	CHECK(FailureMessage(documents.At(0)).find("exist") == std::string::npos);
}

#ifdef CORE_PLATFORM_WINDOWS
TEST_CASE("Opening a file locked by another handle does not claim it does not exist")
{
	TempDirectory directory;
	const std::filesystem::path path = directory.GetPath() / "locked.txt";
	WriteFileBytes(path, "content");

	const HANDLE lockingHandle = ::CreateFileW(path.c_str(), GENERIC_READ, kShareWithNoOne, nullptr,
											   OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
	REQUIRE(lockingHandle != INVALID_HANDLE_VALUE);

	DocumentSet documents;
	const OpenOutcome outcome = documents.Open(path, TabKind::Permanent);

	// Closed before any check can fail the test, so a locked file never reaches TempDirectory's
	// destructor and takes a later test down with it.
	::CloseHandle(lockingHandle);

	CHECK(outcome == OpenOutcome::Failed);
	REQUIRE(documents.GetCount() == 1);
	CHECK(documents.At(0).Failure == OpenFailure::ReadFailed);
	CHECK(documents.At(0).Failure != OpenFailure::Missing);
	CHECK(FailureMessage(documents.At(0)).find("exist") == std::string::npos);
}
#else
TEST_CASE("Opening a file locked by another handle does not claim it does not exist" * doctest::skip())
{
}
#endif

TEST_CASE("A large file asks before it loads, and adds nothing until confirmed")
{
	TempDirectory directory;
	const std::filesystem::path path = directory.GetPath() / "big.log";

	if (!TryWriteLargeTextFile(path, LargeFileThreshold + 1))
	{
		return;
	}

	DocumentSet documents;

	CHECK(documents.Open(path, TabKind::Permanent) == OpenOutcome::NeedsConfirmation);
	CHECK(documents.GetCount() == 0);

	CHECK(documents.OpenConfirmed(path, TabKind::Permanent) == OpenOutcome::Opened);
	CHECK(documents.GetCount() == 1);
	CHECK(documents.At(0).Document.has_value());
}

TEST_CASE("The failure message names the size, so the number is not a mystery")
{
	const OpenItem item{.Failure = OpenFailure::TooLarge, .FileSize = kSevenHundredEightyMegabytes};

	CHECK(FailureMessage(item).find("780") != std::string::npos);
}

TEST_CASE("Closing removes the item and moves the active one")
{
	TempDirectory directory;
	const std::filesystem::path firstFile = directory.GetPath() / "a.txt";
	const std::filesystem::path secondFile = directory.GetPath() / "b.txt";
	WriteFileBytes(firstFile, "a");
	WriteFileBytes(secondFile, "b");

	DocumentSet documents;
	documents.Open(firstFile, TabKind::Permanent);
	documents.Open(secondFile, TabKind::Permanent);

	documents.Close(1);

	CHECK(documents.GetCount() == 1);
	CHECK(documents.GetActiveIndex() == std::optional<std::size_t>{0});
}

TEST_CASE("Closing the last item leaves nothing active")
{
	TempDirectory directory;
	const std::filesystem::path path = directory.GetPath() / "a.txt";
	WriteFileBytes(path, "a");

	DocumentSet documents;
	documents.Open(path, TabKind::Permanent);
	documents.Close(0);

	CHECK(documents.GetCount() == 0);
	CHECK_FALSE(documents.GetActiveIndex().has_value());
}

TEST_CASE("Closing an item before the active one keeps the same item active")
{
	TempDirectory directory;
	const std::filesystem::path firstFile = directory.GetPath() / "a.txt";
	const std::filesystem::path secondFile = directory.GetPath() / "b.txt";
	WriteFileBytes(firstFile, "a");
	WriteFileBytes(secondFile, "b");

	DocumentSet documents;
	documents.Open(firstFile, TabKind::Permanent);
	documents.Open(secondFile, TabKind::Permanent);
	REQUIRE(documents.GetActiveIndex() == std::optional<std::size_t>{1});

	documents.Close(0);

	REQUIRE(documents.GetCount() == 1);
	CHECK(documents.GetActiveIndex() == std::optional<std::size_t>{0});
	CHECK(documents.At(0).Path == secondFile);
}

TEST_CASE("Closing everything empties the set")
{
	TempDirectory directory;
	const std::filesystem::path firstFile = directory.GetPath() / "a.txt";
	const std::filesystem::path secondFile = directory.GetPath() / "b.txt";
	WriteFileBytes(firstFile, "a");
	WriteFileBytes(secondFile, "b");

	DocumentSet documents;
	documents.Open(firstFile, TabKind::Permanent);
	documents.Open(secondFile, TabKind::Permanent);

	documents.CloseAll();

	CHECK(documents.GetCount() == 0);
	CHECK_FALSE(documents.GetActiveIndex().has_value());
}

TEST_CASE("A file can be found by path")
{
	TempDirectory directory;
	const std::filesystem::path path = directory.GetPath() / "a.txt";
	WriteFileBytes(path, "a");

	DocumentSet documents;
	documents.Open(path, TabKind::Permanent);

	CHECK(documents.Find(path) == std::optional<std::size_t>{0});
	CHECK_FALSE(documents.Find(directory.GetPath() / "other.txt").has_value());
}

TEST_CASE("Each item carries its own caret, so switching away and back keeps the place")
{
	TempDirectory directory;
	const std::filesystem::path firstFile = directory.GetPath() / "a.txt";
	const std::filesystem::path secondFile = directory.GetPath() / "b.txt";
	WriteFileBytes(firstFile, "hello");
	WriteFileBytes(secondFile, "world");

	DocumentSet documents;
	documents.Open(firstFile, TabKind::Permanent);
	documents.At(0).Caret.Offset = 3;
	documents.Open(secondFile, TabKind::Permanent);

	documents.Activate(0);

	CHECK(documents.At(0).Caret.Offset == 3);
	CHECK(documents.At(1).Caret.Offset == 0);
}

TEST_CASE("A single click opens a preview and the next one replaces it")
{
	TempDirectory directory;
	const std::filesystem::path firstFile = directory.GetPath() / "a.txt";
	const std::filesystem::path secondFile = directory.GetPath() / "b.txt";
	WriteFileBytes(firstFile, "a");
	WriteFileBytes(secondFile, "b");

	DocumentSet documents;

	documents.Open(firstFile, TabKind::Preview);
	REQUIRE(documents.GetCount() == 1);
	CHECK(documents.GetPreviewIndex() == std::optional<std::size_t>{0});

	documents.Open(secondFile, TabKind::Preview);
	CHECK(documents.GetCount() == 1);
	CHECK(documents.At(0).Path == secondFile);
	CHECK(documents.GetPreviewIndex() == std::optional<std::size_t>{0});
}

TEST_CASE("A double click opens something that does not get replaced")
{
	TempDirectory directory;
	const std::filesystem::path firstFile = directory.GetPath() / "a.txt";
	const std::filesystem::path secondFile = directory.GetPath() / "b.txt";
	WriteFileBytes(firstFile, "a");
	WriteFileBytes(secondFile, "b");

	DocumentSet documents;
	documents.Open(firstFile, TabKind::Permanent);
	documents.Open(secondFile, TabKind::Preview);

	CHECK(documents.GetCount() == 2);
	CHECK_FALSE(documents.At(0).Preview);
	CHECK(documents.At(1).Preview);
}

TEST_CASE("Promoting a preview frees the slot")
{
	TempDirectory directory;
	const std::filesystem::path firstFile = directory.GetPath() / "a.txt";
	const std::filesystem::path secondFile = directory.GetPath() / "b.txt";
	WriteFileBytes(firstFile, "a");
	WriteFileBytes(secondFile, "b");

	DocumentSet documents;
	documents.Open(firstFile, TabKind::Preview);
	documents.Promote(0);

	CHECK_FALSE(documents.GetPreviewIndex().has_value());

	documents.Open(secondFile, TabKind::Preview);

	CHECK(documents.GetCount() == 2);
}

TEST_CASE("Opening an already-open preview permanently promotes it")
{
	TempDirectory directory;
	const std::filesystem::path path = directory.GetPath() / "a.txt";
	WriteFileBytes(path, "a");

	DocumentSet documents;
	documents.Open(path, TabKind::Preview);
	REQUIRE(documents.At(0).Preview);

	documents.Open(path, TabKind::Permanent);

	CHECK_FALSE(documents.At(0).Preview);
	CHECK_FALSE(documents.GetPreviewIndex().has_value());
}

TEST_CASE("Closing the preview clears the slot")
{
	TempDirectory directory;
	const std::filesystem::path path = directory.GetPath() / "a.txt";
	WriteFileBytes(path, "a");

	DocumentSet documents;
	documents.Open(path, TabKind::Preview);
	documents.Close(0);

	CHECK_FALSE(documents.GetPreviewIndex().has_value());
}

TEST_CASE("A title is the file name when nothing collides with it")
{
	TempDirectory directory;
	const std::filesystem::path path = directory.GetPath() / "main.cpp";
	WriteFileBytes(path, "a");

	DocumentSet documents;
	documents.Open(path, TabKind::Permanent);

	CHECK(documents.DisplayTitle(0) == "main.cpp");
}

TEST_CASE("Two files of the same name both gain their parent directory")
{
	TempDirectory directory;
	std::filesystem::create_directories(directory.GetPath() / "Core");
	std::filesystem::create_directories(directory.GetPath() / "Graphite");

	const std::filesystem::path firstFile = directory.GetPath() / "Core" / "main.cpp";
	const std::filesystem::path secondFile = directory.GetPath() / "Graphite" / "main.cpp";
	WriteFileBytes(firstFile, "a");
	WriteFileBytes(secondFile, "b");

	DocumentSet documents;
	documents.Open(firstFile, TabKind::Permanent);
	documents.Open(secondFile, TabKind::Permanent);

	CHECK(documents.DisplayTitle(0) == "Core/main.cpp");
	CHECK(documents.DisplayTitle(1) == "Graphite/main.cpp");
}

TEST_CASE("A third file with a different name is not dragged into the disambiguation")
{
	TempDirectory directory;
	std::filesystem::create_directories(directory.GetPath() / "Core");

	const std::filesystem::path firstFile = directory.GetPath() / "Core" / "main.cpp";
	const std::filesystem::path secondFile = directory.GetPath() / "main.cpp";
	const std::filesystem::path thirdFile = directory.GetPath() / "other.cpp";
	WriteFileBytes(firstFile, "a");
	WriteFileBytes(secondFile, "b");
	WriteFileBytes(thirdFile, "c");

	DocumentSet documents;
	documents.Open(firstFile, TabKind::Permanent);
	documents.Open(secondFile, TabKind::Permanent);
	documents.Open(thirdFile, TabKind::Permanent);

	CHECK(documents.DisplayTitle(2) == "other.cpp");
}

TEST_CASE("Paths follow the workspace root when it moves")
{
	TempDirectory directory;
	const std::filesystem::path path = directory.GetPath() / "a.txt";
	WriteFileBytes(path, "a");

	DocumentSet documents;
	documents.Open(path, TabKind::Permanent);

	documents.RewritePaths(directory.GetPath(), "D:/moved");

	CHECK(documents.At(0).Path == std::filesystem::path{"D:/moved"} / "a.txt");
}

TEST_CASE("A path outside the moved root is left alone")
{
	TempDirectory directory;
	const std::filesystem::path path = directory.GetPath() / "a.txt";
	WriteFileBytes(path, "a");

	DocumentSet documents;
	documents.Open(path, TabKind::Permanent);

	documents.RewritePaths("D:/somewhere-else", "D:/moved");

	CHECK(documents.At(0).Path == path);
}

TEST_CASE("A path equal to the moved root is rewritten without a trailing dot")
{
	TempDirectory directory;

	DocumentSet documents;

	documents.Open(directory.GetPath(), TabKind::Permanent);
	REQUIRE(documents.GetCount() == 1);
	CHECK(documents.At(0).Path == directory.GetPath());
	CHECK(documents.At(0).Failure == OpenFailure::ReadFailed);

	documents.RewritePaths(directory.GetPath(), "D:/moved");

	CHECK(documents.At(0).Path == std::filesystem::path{"D:/moved"});
}

TEST_CASE("A deleted file keeps its window and its content")
{
	TempDirectory directory;
	const std::filesystem::path path = directory.GetPath() / "a.txt";
	WriteFileBytes(path, "still here");

	DocumentSet documents;
	documents.Open(path, TabKind::Permanent);

	documents.MarkMissing(path);

	REQUIRE(documents.GetCount() == 1);
	CHECK(documents.At(0).Failure == OpenFailure::Missing);
	REQUIRE(documents.At(0).Document.has_value());
	CHECK(documents.At(0).Document->GetBuffer().Materialize() == "still here");
}

TEST_CASE("Marking a file that is not open does nothing")
{
	DocumentSet documents;

	documents.MarkMissing("D:/never-opened.txt");

	CHECK(documents.GetCount() == 0);
}

TEST_CASE("Marking a file missing clears any detail an earlier failure left behind")
{
	TempDirectory directory;
	const std::filesystem::path subfolder = directory.GetPath() / "sub";
	std::filesystem::create_directories(subfolder);

	DocumentSet documents;

	documents.Open(subfolder, TabKind::Permanent);

	REQUIRE(documents.GetCount() == 1);
	REQUIRE(documents.At(0).Failure == OpenFailure::ReadFailed);
	REQUIRE_FALSE(documents.At(0).FailureDetail.empty());

	documents.MarkMissing(subfolder);

	CHECK(documents.At(0).Failure == OpenFailure::Missing);
	CHECK(documents.At(0).FailureDetail.empty());
	CHECK(FailureMessage(documents.At(0)).find("exist") != std::string::npos);
}

TEST_CASE("A single click that needs confirmation does not evict the preview")
{
	TempDirectory directory;
	const std::filesystem::path previewedFile = directory.GetPath() / "a.txt";
	WriteFileBytes(previewedFile, "a");

	const std::filesystem::path largeFile = directory.GetPath() / "big.log";

	if (!TryWriteLargeTextFile(largeFile, LargeFileThreshold + 1))
	{
		return;
	}

	DocumentSet documents;
	documents.Open(previewedFile, TabKind::Preview);
	REQUIRE(documents.GetCount() == 1);
	REQUIRE(documents.GetPreviewIndex() == std::optional<std::size_t>{0});

	CHECK(documents.Open(largeFile, TabKind::Preview) == OpenOutcome::NeedsConfirmation);

	CHECK(documents.GetCount() == 1);
	CHECK(documents.At(0).Path == previewedFile);
	CHECK(documents.GetPreviewIndex() == std::optional<std::size_t>{0});
}

TEST_CASE("The document's own path that a save writes to follows the workspace root when it moves")
{
	TempDirectory directory;
	const std::filesystem::path path = directory.GetPath() / "a.txt";
	WriteFileBytes(path, "a");

	DocumentSet documents;
	documents.Open(path, TabKind::Permanent);
	REQUIRE(documents.At(0).Document.has_value());

	documents.RewritePaths(directory.GetPath(), "D:/moved");

	CHECK(documents.At(0).Document->GetPath() == documents.At(0).Path);
	CHECK(documents.At(0).Document->GetPath() == std::filesystem::path{"D:/moved"} / "a.txt");
}

TEST_CASE("A deleted file says so in its tab title")
{
	TempDirectory directory;
	const std::filesystem::path path = directory.GetPath() / "a.txt";
	WriteFileBytes(path, "still here");

	DocumentSet documents;
	documents.Open(path, TabKind::Permanent);

	REQUIRE(documents.DisplayTitle(0) == "a.txt");

	documents.MarkMissing(path);

	CHECK(documents.DisplayTitle(0) == "a.txt (deleted)");
}

TEST_CASE("A deleted file that shares its name keeps the disambiguating directory too")
{
	TempDirectory directory;
	const std::filesystem::path firstFolder = directory.GetPath() / "one";
	const std::filesystem::path secondFolder = directory.GetPath() / "two";
	std::filesystem::create_directories(firstFolder);
	std::filesystem::create_directories(secondFolder);

	WriteFileBytes(firstFolder / "main.cpp", "a");
	WriteFileBytes(secondFolder / "main.cpp", "b");

	DocumentSet documents;
	documents.Open(firstFolder / "main.cpp", TabKind::Permanent);
	documents.Open(secondFolder / "main.cpp", TabKind::Permanent);

	documents.MarkMissing(firstFolder / "main.cpp");

	CHECK(documents.DisplayTitle(0) == "one/main.cpp (deleted)");
	CHECK(documents.DisplayTitle(1) == "two/main.cpp");
}

TEST_CASE("A missing file that comes back stops being marked deleted")
{
	TempDirectory directory;
	const std::filesystem::path path = directory.GetPath() / "a.txt";
	WriteFileBytes(path, "still here");

	DocumentSet documents;
	documents.Open(path, TabKind::Permanent);

	documents.MarkMissing(path);
	REQUIRE(documents.At(0).Failure == OpenFailure::Missing);

	documents.MarkPresent(path);

	CHECK(documents.At(0).Failure == OpenFailure::None);
	CHECK(documents.DisplayTitle(0).find("(deleted)") == std::string::npos);
}

TEST_CASE("MarkPresent leaves a failure that is not about the file being gone since that tab never loaded content")
{
	TempDirectory directory;
	const std::filesystem::path path = directory.GetPath() / "binary.obj";
	WriteFileBytes(path, kBinaryFileBytes);

	DocumentSet documents;
	documents.Open(path, TabKind::Permanent);

	REQUIRE(documents.At(0).Failure == OpenFailure::Binary);

	documents.MarkPresent(path);

	CHECK(documents.At(0).Failure == OpenFailure::Binary);
}

TEST_CASE("Loading fills an item that opened before its file existed")
{
	TempDirectory directory;
	const std::filesystem::path path = directory.GetPath() / "appears.txt";

	DocumentSet documents;
	REQUIRE(documents.Open(path, TabKind::Permanent) == OpenOutcome::Failed);
	REQUIRE_FALSE(documents.At(0).Document.has_value());

	WriteFileBytes(path, "here now\n");

	CHECK(LoadFromFile(documents.At(0), LargeFileConfirmation::Unconfirmed) == ItemLoad::Loaded);
	REQUIRE(documents.At(0).Document.has_value());
	CHECK(documents.At(0).Document->GetBuffer().Materialize() == "here now\n");
	CHECK(documents.At(0).Failure == OpenFailure::None);
	CHECK(documents.At(0).FailureDetail.empty());
}

TEST_CASE("Loading a file that turned out to be binary leaves the reason, not a document")
{
	TempDirectory directory;
	const std::filesystem::path path = directory.GetPath() / "appears.obj";

	DocumentSet documents;
	REQUIRE(documents.Open(path, TabKind::Permanent) == OpenOutcome::Failed);

	WriteFileBytes(path, kBinaryFileBytes);

	CHECK(LoadFromFile(documents.At(0), LargeFileConfirmation::Unconfirmed) == ItemLoad::Failed);
	CHECK_FALSE(documents.At(0).Document.has_value());
	CHECK(documents.At(0).Failure == OpenFailure::Binary);
}

TEST_CASE("Loading a large file asks and leaves the failure in place until confirmed and then reads it")
{
	TempDirectory directory;
	const std::filesystem::path path = directory.GetPath() / "appears.log";

	DocumentSet documents;
	REQUIRE(documents.Open(path, TabKind::Permanent) == OpenOutcome::Failed);

	if (!TryWriteLargeTextFile(path, LargeFileThreshold + 1))
	{
		return;
	}

	const std::string failureDetailBeforeQuestion = documents.At(0).FailureDetail;
	REQUIRE_FALSE(failureDetailBeforeQuestion.empty());

	CHECK(LoadFromFile(documents.At(0), LargeFileConfirmation::Unconfirmed) == ItemLoad::NeedsConfirmation);
	CHECK_FALSE(documents.At(0).Document.has_value());
	CHECK(documents.At(0).Failure == OpenFailure::Missing);
	CHECK(documents.At(0).FailureDetail == failureDetailBeforeQuestion);

	CHECK(LoadFromFile(documents.At(0), LargeFileConfirmation::Confirmed) == ItemLoad::Loaded);
	CHECK(documents.At(0).Document.has_value());
	CHECK(documents.At(0).Failure == OpenFailure::None);
}

TEST_CASE("An open that lands on an existing item asks for its window to be brought forward")
{
	TempDirectory directory;
	const std::filesystem::path firstFile = directory.GetPath() / "a.txt";
	const std::filesystem::path secondFile = directory.GetPath() / "b.txt";
	WriteFileBytes(firstFile, "a");
	WriteFileBytes(secondFile, "b");

	DocumentSet documents;
	documents.Open(firstFile, TabKind::Permanent);
	documents.Open(secondFile, TabKind::Permanent);

	documents.At(0).PendingFocus = false;
	documents.At(1).PendingFocus = false;

	REQUIRE(documents.Open(firstFile, TabKind::Permanent) == OpenOutcome::Activated);
	CHECK(documents.At(0).PendingFocus);
	CHECK_FALSE(documents.At(1).PendingFocus);
}

TEST_CASE("A newly opened item asks for its window too")
{
	TempDirectory directory;
	const std::filesystem::path path = directory.GetPath() / "a.txt";
	WriteFileBytes(path, "a");

	DocumentSet documents;

	REQUIRE(documents.Open(path, TabKind::Permanent) == OpenOutcome::Opened);
	CHECK(documents.At(0).PendingFocus);
}

TEST_CASE("BufferOfOpenDocument finds a tab's buffer only when the tab holds a document")
{
	TempDirectory directory;
	const std::filesystem::path text = directory.MakeFile("a.txt", "Hello");
	const std::filesystem::path binary = directory.MakeFile("b.obj", std::string{kExecutableHeaderBytes});
	DocumentSet documents;

	CHECK(BufferOfOpenDocument(documents, text) == nullptr);

	REQUIRE(documents.Open(text, TabKind::Permanent) == OpenOutcome::Opened);
	const TextBuffer* buffer = BufferOfOpenDocument(documents, text);
	REQUIRE(buffer != nullptr);
	CHECK(buffer->Materialize() == "Hello");

	REQUIRE(documents.Open(binary, TabKind::Permanent) == OpenOutcome::Failed);
	CHECK(BufferOfOpenDocument(documents, binary) == nullptr);

	const std::filesystem::path upperCaseSpellingOfText = directory.GetPath() / "A.TXT";
	CHECK(BufferOfOpenDocument(documents, upperCaseSpellingOfText) == nullptr);
}

TEST_CASE("GetActiveItemPath names the active tab whether or not it holds a document")
{
	TempDirectory directory;
	DocumentSet documents;

	CHECK(documents.GetActiveItemPath() == nullptr);

	const std::filesystem::path text = directory.MakeFile("a.txt", "Hello");
	REQUIRE(documents.Open(text, TabKind::Permanent) == OpenOutcome::Opened);
	REQUIRE(documents.GetActiveItemPath() != nullptr);
	CHECK(*documents.GetActiveItemPath() == text);

	const std::filesystem::path binary = directory.MakeFile("b.obj", std::string{kExecutableHeaderBytes});
	REQUIRE(documents.Open(binary, TabKind::Permanent) == OpenOutcome::Failed);
	REQUIRE(documents.GetActiveItemPath() != nullptr);
	CHECK(*documents.GetActiveItemPath() == binary);
}
