#include <doctest/doctest.h>

#include "Core/App/Redraw.h"
#include "Core/Text/DocumentSet.h"
#include "Core/Text/FileProbe.h"

#include "DeferredJobs.h"
#include "FileFixtures.h"
#include "SparseFileFixture.h"
#include "TempDirectory.h"

#include <cstdint>
#include <filesystem>
#include <ostream>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

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

	std::string TextOfLinesPast(std::uint64_t size, std::string_view line)
	{
		std::string text;
		text.reserve(static_cast<std::size_t>(size) + line.size());

		while (text.size() <= size)
		{
			text += line;
		}

		return text;
	}

	std::string TextReadOnAWorker()
	{
		return TextOfLinesPast(BackgroundLoadThreshold, "\tlog line\n");
	}

	// WARNING: A finished read reaches the set through its mailbox, which asks for a frame; nothing else in a read does.
	bool RunningThemPostsAResult(DeferredJobs& jobs)
	{
		Redraw::TakeRequest();
		jobs.RunAll();

		return Redraw::IsRequested();
	}
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
	REQUIRE(documents.At(0).Document != nullptr);
	CHECK(documents.At(0).Document->GetBuffer().Materialize() == "int main() {}\n");
	CHECK(documents.At(0).Failure == OpenFailure::None);
	CHECK(documents.GetActiveIndex() == std::optional<std::size_t>{0});
}

TEST_CASE("A file with no line break in it is saved with the line ending set for new files and one with breaks keeps its own")
{
	TempDirectory directory;
	const std::filesystem::path empty = directory.GetPath() / "new.cpp";
	const std::filesystem::path lines = directory.GetPath() / "old.cpp";
	WriteFileBytes(empty, "");
	WriteFileBytes(lines, "a\nb\n");

	DocumentSet documents;
	documents.SetLineEndingForNewFiles(LineEnding::Crlf);

	REQUIRE(documents.Open(empty, TabKind::Permanent) == OpenOutcome::Opened);
	REQUIRE(documents.Open(lines, TabKind::Permanent) == OpenOutcome::Opened);

	CHECK(documents.At(0).Document->GetDocumentLineEnding() == LineEnding::Crlf);
	CHECK(documents.At(1).Document->GetDocumentLineEnding() == LineEnding::Lf);
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

TEST_CASE("A binary file that is not an image opens as a hex item")
{
	TempDirectory directory;
	const std::filesystem::path path = directory.GetPath() / "object.obj";
	WriteFileBytes(path, kBinaryFileBytes);

	DocumentSet documents;

	CHECK(documents.Open(path, TabKind::Permanent) == OpenOutcome::Opened);
	REQUIRE(documents.GetCount() == 1);
	CHECK(documents.GetActiveIndex() == std::optional<std::size_t>{0});
	CHECK(documents.At(0).View == ItemView::Hex);
	CHECK(documents.At(0).Failure == OpenFailure::None);
	CHECK(documents.At(0).Document == nullptr);
	CHECK(documents.At(0).FileSize == kBinaryFileBytes.size());
	CHECK_FALSE(IsLoading(documents.At(0)));
}

TEST_CASE("A binary file over 512 MB still opens as a hex item")
{
	TempDirectory directory;
	const std::filesystem::path path = directory.GetPath() / "huge.bin";

	if (!TryWriteLargeBinaryFile(path, MaximumFileSize + 1))
	{
		return;
	}

	DocumentSet documents;

	CHECK(documents.Open(path, TabKind::Permanent) == OpenOutcome::Opened);
	REQUIRE(documents.GetCount() == 1);
	CHECK(documents.At(0).View == ItemView::Hex);
	CHECK(documents.At(0).Failure == OpenFailure::None);
	CHECK(documents.At(0).FileSize == MaximumFileSize + 1);
}

TEST_CASE("A text file over 512 MB is still too large to open")
{
	TempDirectory directory;
	const std::filesystem::path path = directory.GetPath() / "huge.log";

	if (!TryWriteLargeTextFile(path, MaximumFileSize + 1))
	{
		return;
	}

	DocumentSet documents;

	CHECK(documents.Open(path, TabKind::Permanent) == OpenOutcome::Failed);
	REQUIRE(documents.GetCount() == 1);
	CHECK(documents.At(0).View == ItemView::Text);
	CHECK(documents.At(0).Failure == OpenFailure::TooLarge);
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
	REQUIRE(documents.At(0).Document != nullptr);
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
	CHECK(documents.At(0).Document != nullptr);
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
	REQUIRE(documents.At(0).Document != nullptr);
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
	REQUIRE(documents.At(0).Document != nullptr);

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
	const std::filesystem::path path = directory.GetPath() / "huge.log";

	if (!TryWriteLargeTextFile(path, MaximumFileSize + 1))
	{
		return;
	}

	DocumentSet documents;
	documents.Open(path, TabKind::Permanent);

	REQUIRE(documents.At(0).Failure == OpenFailure::TooLarge);

	documents.MarkPresent(path);

	CHECK(documents.At(0).Failure == OpenFailure::TooLarge);
}

TEST_CASE("Loading fills an item that opened before its file existed")
{
	TempDirectory directory;
	const std::filesystem::path path = directory.GetPath() / "appears.txt";

	DocumentSet documents;
	REQUIRE(documents.Open(path, TabKind::Permanent) == OpenOutcome::Failed);
	REQUIRE_FALSE(documents.At(0).Document != nullptr);

	WriteFileBytes(path, "here now\n");

	CHECK(LoadFromFile(documents.At(0), documents.GetStore(), LargeFileConfirmation::Unconfirmed) == ItemLoad::Loaded);
	REQUIRE(documents.At(0).Document != nullptr);
	CHECK(documents.At(0).Document->GetBuffer().Materialize() == "here now\n");
	CHECK(documents.At(0).Failure == OpenFailure::None);
	CHECK(documents.At(0).FailureDetail.empty());
}

TEST_CASE("Loading a file that turned out to be binary shows it as hex, not a document")
{
	TempDirectory directory;
	const std::filesystem::path path = directory.GetPath() / "appears.obj";

	DocumentSet documents;
	REQUIRE(documents.Open(path, TabKind::Permanent) == OpenOutcome::Failed);

	WriteFileBytes(path, kBinaryFileBytes);

	CHECK(LoadFromFile(documents.At(0), documents.GetStore(), LargeFileConfirmation::Unconfirmed) == ItemLoad::Loaded);
	CHECK_FALSE(documents.At(0).Document != nullptr);
	CHECK(documents.At(0).View == ItemView::Hex);
	CHECK(documents.At(0).Failure == OpenFailure::None);
}

// The classification needs only a NUL in the first bytes and the extension, so these files hold the
// generic binary bytes, not real pictures.
TEST_CASE("A binary png opens as an image item with no failure")
{
	TempDirectory directory;
	const std::filesystem::path path = directory.GetPath() / "logo.PNG";
	WriteFileBytes(path, kBinaryFileBytes);

	DocumentSet documents;

	CHECK(documents.Open(path, TabKind::Permanent) == OpenOutcome::Opened);
	CHECK(documents.At(0).View == ItemView::Image);
	CHECK(documents.At(0).Failure == OpenFailure::None);
	CHECK(documents.At(0).Document == nullptr);
	CHECK(documents.At(0).FileSize == kBinaryFileBytes.size());
}

TEST_CASE("A png that holds text, such as a Git LFS pointer, opens as text")
{
	TempDirectory directory;
	const std::filesystem::path path = directory.GetPath() / "logo.png";
	WriteFileBytes(path, "version https://git-lfs.github.com/spec/v1\noid sha256:abc\nsize 12345\n");

	DocumentSet documents;

	CHECK(documents.Open(path, TabKind::Permanent) == OpenOutcome::Opened);
	CHECK(documents.At(0).View == ItemView::Text);
	CHECK(documents.At(0).Document != nullptr);
}

TEST_CASE("A binary file of another kind opens as hex, not as an image")
{
	TempDirectory directory;
	const std::filesystem::path path = directory.GetPath() / "program.exe";
	WriteFileBytes(path, kBinaryFileBytes);

	DocumentSet documents;

	CHECK(documents.Open(path, TabKind::Permanent) == OpenOutcome::Opened);
	CHECK(documents.At(0).View == ItemView::Hex);
	CHECK(documents.At(0).Failure == OpenFailure::None);
}

TEST_CASE("An image that went missing and came back opens as an image again")
{
	TempDirectory directory;
	const std::filesystem::path path = directory.GetPath() / "photo.jpg";
	WriteFileBytes(path, kBinaryFileBytes);

	DocumentSet documents;
	REQUIRE(documents.Open(path, TabKind::Permanent) == OpenOutcome::Opened);

	documents.MarkMissing(path);
	REQUIRE(documents.At(0).Failure == OpenFailure::Missing);

	documents.MarkPresent(path);

	CHECK(LoadFromFile(documents.At(0), documents.GetStore(), LargeFileConfirmation::Unconfirmed) == ItemLoad::Loaded);
	CHECK(documents.At(0).View == ItemView::Image);
	CHECK(documents.At(0).Failure == OpenFailure::None);
	CHECK(documents.At(0).Document == nullptr);
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

	CHECK(LoadFromFile(documents.At(0), documents.GetStore(), LargeFileConfirmation::Unconfirmed) == ItemLoad::NeedsConfirmation);
	CHECK_FALSE(documents.At(0).Document != nullptr);
	CHECK(documents.At(0).Failure == OpenFailure::Missing);
	CHECK(documents.At(0).FailureDetail == failureDetailBeforeQuestion);

	CHECK(LoadFromFile(documents.At(0), documents.GetStore(), LargeFileConfirmation::Confirmed) == ItemLoad::Loaded);
	CHECK(documents.At(0).Document != nullptr);
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

	REQUIRE(documents.Open(binary, TabKind::Permanent) == OpenOutcome::Opened);
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
	REQUIRE(documents.Open(binary, TabKind::Permanent) == OpenOutcome::Opened);
	REQUIRE(documents.GetActiveItemPath() != nullptr);
	CHECK(*documents.GetActiveItemPath() == binary);
}

TEST_CASE("A file above the background threshold opens as a tab that holds no document until a worker has read it")
{
	TempDirectory directory;
	const std::filesystem::path path = directory.GetPath() / "big.log";
	const std::string text = TextReadOnAWorker();
	WriteFileBytes(path, text);

	DeferredJobs jobs;
	DocumentSet documents;
	documents.LoadLargeFilesWith(jobs.Submitter());

	REQUIRE(documents.Open(path, TabKind::Permanent) == OpenOutcome::Loading);
	REQUIRE(documents.GetCount() == 1);
	CHECK(IsLoading(documents.At(0)));
	CHECK(documents.At(0).Document == nullptr);
	CHECK(documents.At(0).FileSize == text.size());
	CHECK(documents.GetActiveIndex() == std::optional<std::size_t>{0});
	CHECK(documents.GetStore().Find(path) == nullptr);
	CHECK(jobs.Count() == 1);
	CHECK(documents.TakeFinishedLoads().empty());

	jobs.RunAll();
	const std::vector<std::size_t> landedIndices = documents.TakeFinishedLoads();

	REQUIRE(landedIndices == std::vector<std::size_t>{0});
	const OpenItem& item = documents.At(0);
	CHECK_FALSE(IsLoading(item));
	REQUIRE(item.Document != nullptr);
	CHECK(item.Document->GetBuffer().GetLength() == text.size());
	CHECK(item.Failure == OpenFailure::None);
	CHECK(documents.GetStore().Find(path) == item.Document);
}

TEST_CASE("A document read on a worker is measured exactly as one read where it is opened")
{
	TempDirectory directory;
	const std::filesystem::path path = directory.GetPath() / "big.log";
	WriteFileBytes(path, TextOfLinesPast(BackgroundLoadThreshold, "if (x)\n\treturn widest;\n\n"));

	DocumentSet readWhereOpened;
	REQUIRE(readWhereOpened.Open(path, TabKind::Permanent) == OpenOutcome::Opened);

	DeferredJobs jobs;
	DocumentSet readOnAWorker;
	readOnAWorker.LoadLargeFilesWith(jobs.Submitter());
	REQUIRE(readOnAWorker.Open(path, TabKind::Permanent) == OpenOutcome::Loading);
	jobs.RunAll();
	REQUIRE(readOnAWorker.TakeFinishedLoads().size() == 1);

	const OpenItem& expected = readWhereOpened.At(0);
	const OpenItem& actual = readOnAWorker.At(0);

	CHECK(actual.TabWidth == expected.TabWidth);
	CHECK(actual.WidestColumn == expected.WidestColumn);
	CHECK(actual.WidestLine == expected.WidestLine);
	CHECK(actual.IndentDepths == expected.IndentDepths);
	CHECK(actual.Rows.GetLineCount() == expected.Rows.GetLineCount());
	CHECK(actual.Rows.GetTotalRows() == expected.Rows.GetTotalRows());
	CHECK(actual.SeenChange == expected.SeenChange);
}

TEST_CASE("A document read on a worker names the unwrapped rows it was built with so the first draw does not wrap it again")
{
	TempDirectory directory;
	const std::filesystem::path path = directory.GetPath() / "big.log";
	WriteFileBytes(path, TextReadOnAWorker());

	DeferredJobs jobs;
	DocumentSet documents;
	documents.LoadLargeFilesWith(jobs.Submitter());
	REQUIRE(documents.Open(path, TabKind::Permanent) == OpenOutcome::Loading);
	jobs.RunAll();
	REQUIRE(documents.TakeFinishedLoads().size() == 1);

	CHECK(documents.At(0).Rows.GetColumnsPerRow() == RowMap::kNoWrap);
	CHECK(documents.At(0).ColumnsPerWrappedRow == RowMap::kNoWrap);
}

TEST_CASE("A file no larger than the background threshold opens where it is asked for")
{
	TempDirectory directory;
	const std::filesystem::path path = directory.GetPath() / "edge.log";
	WriteFileBytes(path, std::string(static_cast<std::size_t>(BackgroundLoadThreshold), 'a'));

	DeferredJobs jobs;
	DocumentSet documents;
	documents.LoadLargeFilesWith(jobs.Submitter());

	CHECK(documents.Open(path, TabKind::Permanent) == OpenOutcome::Opened);
	CHECK(jobs.Count() == 0);
	CHECK(documents.At(0).Document != nullptr);
	CHECK_FALSE(IsLoading(documents.At(0)));
}

TEST_CASE("A large file asks before a worker starts to read it")
{
	TempDirectory directory;
	const std::filesystem::path path = directory.GetPath() / "huge.log";

	REQUIRE(TryWriteLargeTextFile(path, LargeFileThreshold + 1));

	DeferredJobs jobs;
	DocumentSet documents;
	documents.LoadLargeFilesWith(jobs.Submitter());

	CHECK(documents.Open(path, TabKind::Permanent) == OpenOutcome::NeedsConfirmation);
	CHECK(documents.GetCount() == 0);
	CHECK(jobs.Count() == 0);

	CHECK(documents.OpenConfirmed(path, TabKind::Permanent) == OpenOutcome::Loading);
	CHECK(documents.GetCount() == 1);
	CHECK(jobs.Count() == 1);
}

TEST_CASE("Closing a tab while its file is read drops what the read brings back")
{
	TempDirectory directory;
	const std::filesystem::path path = directory.GetPath() / "big.log";
	WriteFileBytes(path, TextReadOnAWorker());

	DeferredJobs jobs;
	DocumentSet documents;
	documents.LoadLargeFilesWith(jobs.Submitter());
	REQUIRE(documents.Open(path, TabKind::Permanent) == OpenOutcome::Loading);

	documents.Close(0);

	CHECK_FALSE(RunningThemPostsAResult(jobs));
	CHECK(documents.TakeFinishedLoads().empty());
	CHECK(documents.GetCount() == 0);
	CHECK(documents.GetStore().GetDocuments().empty());
}

TEST_CASE("A read that finished for a tab closed before the set took it is dropped")
{
	TempDirectory directory;
	const std::filesystem::path path = directory.GetPath() / "big.log";
	WriteFileBytes(path, TextReadOnAWorker());

	DeferredJobs jobs;
	DocumentSet documents;
	documents.LoadLargeFilesWith(jobs.Submitter());
	REQUIRE(documents.Open(path, TabKind::Permanent) == OpenOutcome::Loading);

	jobs.RunAll();
	documents.Close(0);

	CHECK(documents.TakeFinishedLoads().empty());
	CHECK(documents.GetStore().GetDocuments().empty());
}

TEST_CASE("Opening a file again while it is read joins the read already running")
{
	TempDirectory directory;
	const std::filesystem::path path = directory.GetPath() / "big.log";
	WriteFileBytes(path, TextReadOnAWorker());

	DeferredJobs jobs;
	DocumentSet documents;
	documents.LoadLargeFilesWith(jobs.Submitter());
	REQUIRE(documents.Open(path, TabKind::Preview) == OpenOutcome::Loading);

	CHECK(documents.Open(path, TabKind::Permanent) == OpenOutcome::Activated);
	CHECK(documents.GetCount() == 1);
	CHECK_FALSE(documents.At(0).Preview);
	CHECK(jobs.Count() == 1);

	jobs.RunAll();
	CHECK(documents.TakeFinishedLoads() == std::vector<std::size_t>{0});
}

TEST_CASE("A read that fails says why in its tab and can be asked again")
{
	TempDirectory directory;
	const std::filesystem::path path = directory.GetPath() / "big.log";
	const std::string text = TextReadOnAWorker();
	WriteFileBytes(path, text);

	DeferredJobs jobs;
	DocumentSet documents;
	documents.LoadLargeFilesWith(jobs.Submitter());
	REQUIRE(documents.Open(path, TabKind::Permanent) == OpenOutcome::Loading);

	std::filesystem::remove(path);
	std::filesystem::create_directory(path);
	jobs.RunAll();

	CHECK(documents.TakeFinishedLoads().empty());
	const OpenItem& failed = documents.At(0);
	CHECK_FALSE(IsLoading(failed));
	CHECK(failed.Document == nullptr);
	CHECK(failed.Failure == OpenFailure::ReadFailed);
	CHECK(FailureMessage(failed).find("Not a file") != std::string::npos);
	CHECK(CanRetryLoad(failed));

	std::filesystem::remove(path);
	WriteFileBytes(path, text);

	CHECK(documents.LoadAgain(0, LargeFileConfirmation::Confirmed) == ItemLoad::Loading);
	CHECK(IsLoading(documents.At(0)));
	jobs.RunAll();

	CHECK(documents.TakeFinishedLoads() == std::vector<std::size_t>{0});
	CHECK(documents.At(0).Document != nullptr);
	CHECK(documents.At(0).Failure == OpenFailure::None);
	CHECK_FALSE(CanRetryLoad(documents.At(0)));
}

TEST_CASE("Only a read that failed offers to try again")
{
	const OpenItem refusedAsBinary{.Failure = OpenFailure::TooLarge};
	const OpenItem gone{.Failure = OpenFailure::Missing};
	const OpenItem unreadable{.Failure = OpenFailure::ReadFailed};

	CHECK_FALSE(CanRetryLoad(refusedAsBinary));
	CHECK_FALSE(CanRetryLoad(gone));
	CHECK(CanRetryLoad(unreadable));
}

TEST_CASE("A tab whose file is read names the file and its size")
{
	const OpenItem item{.Path = "C:/logs/Foo.log", .FileSize = 100ull * 1024 * 1024};

	CHECK(LoadingMessage(item) == "Opening Foo.log (100 MB)");
}

TEST_CASE("A caret placed while the file is read is placed in its text once it lands and the last placement wins")
{
	TempDirectory directory;
	const std::filesystem::path path = directory.GetPath() / "big.log";
	WriteFileBytes(path, TextReadOnAWorker());

	DeferredJobs jobs;
	DocumentSet documents;
	documents.LoadLargeFilesWith(jobs.Submitter());
	REQUIRE(documents.Open(path, TabKind::Permanent) == OpenOutcome::Loading);

	constexpr std::size_t kFirstPlacement = 5;
	constexpr std::size_t kLastPlacement = 7;
	documents.At(0).CaretOnLoad = CaretWhenLoaded{
		.Place = [](OpenItem& loaded) { loaded.Caret = Caret{.Offset = kFirstPlacement, .Anchor = kFirstPlacement}; }};
	documents.At(0).CaretOnLoad = CaretWhenLoaded{
		.Place = [](OpenItem& loaded) { loaded.Caret = Caret{.Offset = kLastPlacement, .Anchor = kLastPlacement}; }};

	jobs.RunAll();
	REQUIRE(documents.TakeFinishedLoads().size() == 1);

	CHECK(documents.At(0).Caret.Offset == kLastPlacement);
	CHECK_FALSE(documents.At(0).CaretOnLoad.has_value());
}

TEST_CASE("A read on a worker measures the text with the tab width set for loads")
{
	TempDirectory directory;
	const std::filesystem::path path = directory.GetPath() / "big.log";
	WriteFileBytes(path, TextOfLinesPast(BackgroundLoadThreshold, "\tx\n"));

	constexpr std::size_t kTabWidth = 8;
	constexpr std::size_t kColumnsOfTabThenX = 9;

	DeferredJobs jobs;
	DocumentSet documents;
	documents.LoadLargeFilesWith(jobs.Submitter());
	documents.SetTabWidthForLoads(kTabWidth);
	REQUIRE(documents.Open(path, TabKind::Permanent) == OpenOutcome::Loading);
	jobs.RunAll();
	REQUIRE(documents.TakeFinishedLoads().size() == 1);

	CHECK(documents.At(0).TabWidth == kTabWidth);
	CHECK(documents.At(0).WidestColumn == kColumnsOfTabThenX);
}

TEST_CASE("A read that fails because its folder was renamed before it began reads the file again at the tab's new path")
{
	TempDirectory directory;
	const std::filesystem::path folder = directory.GetPath() / "logs";
	std::filesystem::create_directory(folder);
	const std::filesystem::path path = folder / "big.log";
	WriteFileBytes(path, TextReadOnAWorker());

	DeferredJobs jobs;
	DocumentSet documents;
	documents.LoadLargeFilesWith(jobs.Submitter());
	REQUIRE(documents.Open(path, TabKind::Permanent) == OpenOutcome::Loading);

	const std::filesystem::path movedFolder = directory.GetPath() / "moved";
	std::filesystem::rename(folder, movedFolder);
	documents.RewritePaths(folder, movedFolder);

	jobs.RunAll();
	CHECK(documents.TakeFinishedLoads().empty());
	REQUIRE(IsLoading(documents.At(0)));
	CHECK(documents.At(0).Failure == OpenFailure::None);

	jobs.RunAll();
	REQUIRE(documents.TakeFinishedLoads().size() == 1);

	const std::filesystem::path movedPath = movedFolder / "big.log";
	REQUIRE(documents.At(0).Document != nullptr);
	CHECK(documents.At(0).Document->GetPath() == movedPath);
	CHECK(documents.GetStore().Find(movedPath) == documents.At(0).Document);
}

TEST_CASE("A file deleted while it is read still says it is deleted once the read lands")
{
	TempDirectory directory;
	const std::filesystem::path path = directory.GetPath() / "big.log";
	WriteFileBytes(path, TextReadOnAWorker());

	DeferredJobs jobs;
	DocumentSet documents;
	documents.LoadLargeFilesWith(jobs.Submitter());
	REQUIRE(documents.Open(path, TabKind::Permanent) == OpenOutcome::Loading);

	documents.MarkMissing(path);
	jobs.RunAll();
	REQUIRE(documents.TakeFinishedLoads().size() == 1);

	CHECK(documents.At(0).Document != nullptr);
	CHECK(documents.At(0).Failure == OpenFailure::Missing);
}

TEST_CASE("Closing every tab stops every read")
{
	TempDirectory directory;
	const std::filesystem::path first = directory.GetPath() / "first.log";
	const std::filesystem::path second = directory.GetPath() / "second.log";
	WriteFileBytes(first, TextReadOnAWorker());
	WriteFileBytes(second, TextReadOnAWorker());

	DeferredJobs jobs;
	DocumentSet documents;
	documents.LoadLargeFilesWith(jobs.Submitter());
	REQUIRE(documents.Open(first, TabKind::Permanent) == OpenOutcome::Loading);
	REQUIRE(documents.Open(second, TabKind::Permanent) == OpenOutcome::Loading);

	documents.CloseAll();

	CHECK_FALSE(RunningThemPostsAResult(jobs));
	CHECK(documents.TakeFinishedLoads().empty());
	CHECK(documents.GetStore().GetDocuments().empty());
}

TEST_CASE("A tab opened before its large file existed reads it on a worker once it is asked to load")
{
	TempDirectory directory;
	const std::filesystem::path path = directory.GetPath() / "appears.log";

	DeferredJobs jobs;
	DocumentSet documents;
	documents.LoadLargeFilesWith(jobs.Submitter());
	REQUIRE(documents.Open(path, TabKind::Permanent) == OpenOutcome::Failed);

	WriteFileBytes(path, TextReadOnAWorker());

	CHECK(documents.LoadAgain(0, LargeFileConfirmation::Unconfirmed) == ItemLoad::Loading);
	jobs.RunAll();
	CHECK(documents.TakeFinishedLoads() == std::vector<std::size_t>{0});
	CHECK(documents.At(0).Document != nullptr);
}

TEST_CASE("Destroying the set stops every read it started")
{
	TempDirectory directory;
	const std::filesystem::path path = directory.GetPath() / "big.log";
	WriteFileBytes(path, TextReadOnAWorker());

	DeferredJobs jobs;

	{
		DocumentSet documents;
		documents.LoadLargeFilesWith(jobs.Submitter());
		REQUIRE(documents.Open(path, TabKind::Permanent) == OpenOutcome::Loading);
	}

	CHECK(jobs.Count() == 1);
	CHECK_FALSE(RunningThemPostsAResult(jobs));
}

TEST_CASE("Cancelling every read keeps the tabs but lets no read finish")
{
	TempDirectory directory;
	const std::filesystem::path path = directory.GetPath() / "big.log";
	WriteFileBytes(path, TextReadOnAWorker());

	DeferredJobs jobs;
	DocumentSet documents;
	documents.LoadLargeFilesWith(jobs.Submitter());
	REQUIRE(documents.Open(path, TabKind::Permanent) == OpenOutcome::Loading);

	documents.CancelAllReads();

	CHECK_FALSE(RunningThemPostsAResult(jobs));
	CHECK(documents.GetCount() == 1);
}

TEST_CASE("A read on a worker wraps its rows as the wrap set for loads asks for the document's line count")
{
	TempDirectory directory;
	const std::filesystem::path path = directory.GetPath() / "big.log";
	const std::string text = TextReadOnAWorker();
	WriteFileBytes(path, text);

	constexpr std::size_t kWrapColumns = 7;
	std::size_t lineCountAskedAbout = 0;

	DeferredJobs jobs;
	DocumentSet documents;
	documents.LoadLargeFilesWith(jobs.Submitter());
	REQUIRE(documents.Open(path, TabKind::Permanent) == OpenOutcome::Loading);

	documents.SetWrapForLoads([&lineCountAskedAbout](std::size_t lineCount) {
		lineCountAskedAbout = lineCount;
		return kWrapColumns;
	});

	jobs.RunAll();
	REQUIRE(documents.TakeFinishedLoads().size() == 1);

	const OpenItem& item = documents.At(0);
	CHECK(lineCountAskedAbout == item.Document->GetBuffer().GetLineCount());
	CHECK(item.Rows.GetColumnsPerRow() == kWrapColumns);
	CHECK(item.ColumnsPerWrappedRow == kWrapColumns);
	CHECK(item.Rows.GetTotalRows() > item.Rows.GetLineCount());
}

TEST_CASE("Retry asks about the size of a file that grew past the question since it was opened")
{
	TempDirectory directory;
	const std::filesystem::path path = directory.GetPath() / "big.log";
	WriteFileBytes(path, TextReadOnAWorker());

	DeferredJobs jobs;
	DocumentSet documents;
	documents.LoadLargeFilesWith(jobs.Submitter());
	REQUIRE(documents.Open(path, TabKind::Permanent) == OpenOutcome::Loading);

	std::filesystem::remove(path);
	std::filesystem::create_directory(path);
	jobs.RunAll();
	REQUIRE(documents.TakeFinishedLoads().empty());
	REQUIRE(CanRetryLoad(documents.At(0)));

	std::filesystem::remove(path);
	REQUIRE(TryWriteLargeTextFile(path, LargeFileThreshold + 1));

	CHECK(documents.RetryLoad(0) == ItemLoad::NeedsConfirmation);
	CHECK_FALSE(IsLoading(documents.At(0)));
	CHECK(jobs.Count() == 0);
}

TEST_CASE("Retry does not ask again about a size above the question that was accepted")
{
	TempDirectory directory;
	const std::filesystem::path path = directory.GetPath() / "huge.log";
	REQUIRE(TryWriteLargeTextFile(path, LargeFileThreshold + 1));

	DeferredJobs jobs;
	DocumentSet documents;
	documents.LoadLargeFilesWith(jobs.Submitter());
	REQUIRE(documents.OpenConfirmed(path, TabKind::Permanent) == OpenOutcome::Loading);

	std::filesystem::remove(path);
	std::filesystem::create_directory(path);
	jobs.RunAll();
	REQUIRE(documents.TakeFinishedLoads().empty());
	REQUIRE(CanRetryLoad(documents.At(0)));

	std::filesystem::remove(path);
	REQUIRE(TryWriteLargeTextFile(path, LargeFileThreshold + 1));

	CHECK(documents.RetryLoad(0) == ItemLoad::Loading);
}

TEST_CASE("A size question answered with no leaves the tab's size as it was")
{
	TempDirectory directory;
	const std::filesystem::path path = directory.GetPath() / "appears.log";

	DocumentSet documents;
	REQUIRE(documents.Open(path, TabKind::Permanent) == OpenOutcome::Failed);
	REQUIRE(TryWriteLargeTextFile(path, LargeFileThreshold + 1));

	REQUIRE(documents.LoadAgain(0, LargeFileConfirmation::Unconfirmed) == ItemLoad::NeedsConfirmation);
	CHECK(documents.At(0).FileSize == 0);
}
TEST_CASE("A caret asked for while a file is read is placed when the read again after a folder rename happens at once")
{
	TempDirectory directory;
	const std::filesystem::path folder = directory.GetPath() / "logs";
	std::filesystem::create_directory(folder);
	const std::filesystem::path path = folder / "big.log";
	WriteFileBytes(path, TextReadOnAWorker());

	DeferredJobs jobs;
	DocumentSet documents;
	documents.LoadLargeFilesWith(jobs.Submitter());
	REQUIRE(documents.Open(path, TabKind::Permanent) == OpenOutcome::Loading);

	constexpr std::size_t kAskedOffset = 3;
	documents.At(0).CaretOnLoad = CaretWhenLoaded{.Place = [](OpenItem& loaded) { loaded.Caret = Caret{.Offset = kAskedOffset, .Anchor = kAskedOffset}; }};

	const std::filesystem::path movedFolder = directory.GetPath() / "moved";
	std::filesystem::rename(folder, movedFolder);
	WriteFileBytes(movedFolder / "big.log", "small enough to read at once\n");
	documents.RewritePaths(folder, movedFolder);

	jobs.RunAll();

	REQUIRE(documents.TakeFinishedLoads() == std::vector<std::size_t>{0});
	REQUIRE(documents.At(0).Document != nullptr);
	CHECK(documents.At(0).Caret.Offset == kAskedOffset);
	CHECK_FALSE(documents.At(0).CaretOnLoad.has_value());
}

TEST_CASE("A read whose own pane handed it a wrap uses that one rather than the wrap for every load")
{
	TempDirectory directory;
	const std::filesystem::path path = directory.GetPath() / "big.log";
	WriteFileBytes(path, TextReadOnAWorker());

	constexpr std::size_t kWrapOfTheLastDrawnPane = 7;
	constexpr std::size_t kWrapOfItsOwnPane = 11;

	DeferredJobs jobs;
	DocumentSet documents;
	documents.LoadLargeFilesWith(jobs.Submitter());
	REQUIRE(documents.Open(path, TabKind::Permanent) == OpenOutcome::Loading);

	documents.SetWrapForLoads([](std::size_t) { return kWrapOfTheLastDrawnPane; });
	documents.SetWrapForLoad(0, [](std::size_t) { return kWrapOfItsOwnPane; });

	jobs.RunAll();
	REQUIRE(documents.TakeFinishedLoads().size() == 1);

	CHECK(documents.At(0).Rows.GetColumnsPerRow() == kWrapOfItsOwnPane);
}