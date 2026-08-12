#include <doctest/doctest.h>

#include "Core/Text/DocumentSet.h"
#include "Core/Text/FileProbe.h"

#include "TempDirectory.h"

#include <fstream>
#include <ostream>
#include <string>

#ifdef CORE_PLATFORM_WINDOWS
    #include <windows.h>

    #include <winioctl.h>
#endif

using namespace Core;

namespace
{
    void WriteBytes(const std::filesystem::path& path, std::string_view contents)
    {
        std::ofstream stream{path, std::ios::binary};
        stream.write(contents.data(), static_cast<std::streamsize>(contents.size()));
    }

#ifdef CORE_PLATFORM_WINDOWS
    // Marks the file sparse so growing it costs no disk space: NTFS reserves real
    // clusters for a grown file unless it is sparse, in which case the unwritten range
    // is free. Returns false if the filesystem cannot do this, letting the caller skip
    // rather than fail — that is a fact about the machine, not the code under test.
    bool MarkSparse(const std::filesystem::path& path)
    {
        const HANDLE handle = ::CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr,
                                            OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (handle == INVALID_HANDLE_VALUE)
        {
            return false;
        }

        DWORD bytesReturned = 0;
        const BOOL succeeded =
            ::DeviceIoControl(handle, FSCTL_SET_SPARSE, nullptr, 0, nullptr, 0, &bytesReturned, nullptr);

        ::CloseHandle(handle);

        return succeeded != FALSE;
    }
#endif
}

TEST_CASE("A new set is empty")
{
    const DocumentSet documents;

    CHECK(documents.Count() == 0);
    CHECK_FALSE(documents.Active().has_value());
}

TEST_CASE("Opening a text file adds an item holding the document")
{
    TempDirectory directory;
    const auto path = directory.Path() / "main.cpp";
    WriteBytes(path, "int main() {}\n");

    DocumentSet documents;

    CHECK(documents.Open(path, true) == OpenOutcome::Opened);
    REQUIRE(documents.Count() == 1);
    REQUIRE(documents.At(0).document.has_value());
    CHECK(documents.At(0).document->Buffer().Text() == "int main() {}\n");
    CHECK(documents.At(0).failure == OpenFailure::None);
    CHECK(documents.Active() == std::optional<std::size_t>{0});
}

TEST_CASE("The widest line is measured in columns, not bytes")
{
    TempDirectory directory;
    const auto path = directory.Path() / "widths.txt";
    // The Polish line is longer in bytes (9 two-byte characters, 18 bytes) than the
    // ASCII line (11 one-byte characters, 11 bytes), but narrower in columns: 9 against
    // 11. A widestColumn computed from byte lengths would pick the Polish line; the
    // correct answer is the ASCII one.
    WriteBytes(path, "\xc4\x85\xc4\x87\xc4\x99\xc5\x82\xc5\x84\xc3\xb3\xc5\x9b\xc5\xba\xc5\xbc\n"
                     "abcdefghijk\n");

    DocumentSet documents;

    CHECK(documents.Open(path, true) == OpenOutcome::Opened);
    REQUIRE(documents.Count() == 1);
    CHECK(documents.At(0).widestColumn == 11);
}

TEST_CASE("Indent depths are computed once when the document loads")
{
    TempDirectory directory;
    const auto path = directory.Path() / "indent.cpp";
    WriteBytes(path, "if (x)\n    return;");

    DocumentSet documents;

    CHECK(documents.Open(path, true) == OpenOutcome::Opened);
    REQUIRE(documents.Count() == 1);
    REQUIRE(documents.At(0).indentDepths.size() == 2);
    CHECK(documents.At(0).indentDepths[0] == 0);
    CHECK(documents.At(0).indentDepths[1] == 4);
}

TEST_CASE("Opening the same file twice activates it instead of duplicating it")
{
    TempDirectory directory;
    const auto first = directory.Path() / "a.txt";
    const auto second = directory.Path() / "b.txt";
    WriteBytes(first, "a");
    WriteBytes(second, "b");

    DocumentSet documents;
    documents.Open(first, true);
    documents.Open(second, true);
    REQUIRE(documents.Count() == 2);
    REQUIRE(documents.Active() == std::optional<std::size_t>{1});

    CHECK(documents.Open(first, true) == OpenOutcome::Activated);
    CHECK(documents.Count() == 2);
    CHECK(documents.Active() == std::optional<std::size_t>{0});
}

TEST_CASE("A binary file opens as an item with a reason rather than nothing at all")
{
    TempDirectory directory;
    const auto path = directory.Path() / "object.obj";
    WriteBytes(path, std::string("MZ\0\0binary", 10));

    DocumentSet documents;

    CHECK(documents.Open(path, true) == OpenOutcome::Failed);
    REQUIRE(documents.Count() == 1);
    CHECK_FALSE(documents.At(0).document.has_value());
    CHECK(documents.At(0).failure == OpenFailure::Binary);
    CHECK(FailureMessage(documents.At(0)).find("Binary") != std::string::npos);
}

TEST_CASE("A UTF-16 file now opens as a document rather than a refusal")
{
    TempDirectory directory;
    const auto path = directory.Path() / "utf16.txt";
    WriteBytes(path, std::string("\xFF\xFEH\0e\0l\0l\0o\0", 12));

    DocumentSet documents;

    CHECK(documents.Open(path, true) == OpenOutcome::Opened);
    REQUIRE(documents.Count() == 1);
    REQUIRE(documents.At(0).document.has_value());
    CHECK(documents.At(0).document->Buffer().Text() == "Hello");
}

TEST_CASE("A damaged UTF-16 file says it is damaged, not that the format is unsupported")
{
    TempDirectory directory;
    const auto path = directory.Path() / "damaged.txt";
    WriteBytes(path, std::string("\xFF\xFE\x42\xDE", 4));

    DocumentSet documents;

    CHECK(documents.Open(path, true) == OpenOutcome::Failed);
    REQUIRE(documents.Count() == 1);
    CHECK(documents.At(0).failure == OpenFailure::ReadFailed);
    CHECK(FailureMessage(documents.At(0)).find("damaged") != std::string::npos);
}

TEST_CASE("A file that is not there opens as an item saying so, not as silence")
{
    TempDirectory directory;

    DocumentSet documents;

    CHECK(documents.Open(directory.Path() / "absent.txt", true) == OpenOutcome::Failed);
    REQUIRE(documents.Count() == 1);
    CHECK(documents.At(0).failure == OpenFailure::Missing);
}

TEST_CASE("Opening a directory does not claim it does not exist")
{
    TempDirectory directory;
    const auto sub = directory.MakeDir("subfolder");

    DocumentSet documents;

    CHECK(documents.Open(sub, true) == OpenOutcome::Failed);
    REQUIRE(documents.Count() == 1);
    CHECK(documents.At(0).failure == OpenFailure::ReadFailed);
    CHECK(documents.At(0).failure != OpenFailure::Missing);
    CHECK(FailureMessage(documents.At(0)).find("exist") == std::string::npos);
}

#ifdef CORE_PLATFORM_WINDOWS
TEST_CASE("Opening a file locked by another handle does not claim it does not exist")
{
    // A sharing violation from a handle held open in-process, deterministic and
    // requiring no second process: opening with dwShareMode 0 denies every other
    // handle, including the ifstream ProbeFile opens next.
    TempDirectory directory;
    const auto path = directory.Path() / "locked.txt";
    WriteBytes(path, "content");

    const HANDLE handle = ::CreateFileW(path.c_str(), GENERIC_READ, /*dwShareMode=*/0, nullptr,
                                        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    REQUIRE(handle != INVALID_HANDLE_VALUE);

    DocumentSet documents;
    const OpenOutcome outcome = documents.Open(path, true);

    // Closed before any assertion can fail the test, so a locked file never survives
    // into TempDirectory's destructor and takes a later test down with it.
    ::CloseHandle(handle);

    CHECK(outcome == OpenOutcome::Failed);
    REQUIRE(documents.Count() == 1);
    CHECK(documents.At(0).failure == OpenFailure::ReadFailed);
    CHECK(documents.At(0).failure != OpenFailure::Missing);
    CHECK(FailureMessage(documents.At(0)).find("exist") == std::string::npos);
}
#else
TEST_CASE("Opening a file locked by another handle does not claim it does not exist"
          * doctest::skip())
{
}
#endif

TEST_CASE("A large file asks before it loads, and adds nothing until confirmed")
{
    TempDirectory directory;
    const auto path = directory.Path() / "big.log";

    // The probe classifies from the first ProbeSampleSize bytes only, so those bytes
    // have to be real text. Growing the file with resize_file zero-fills the new range,
    // and a NUL byte anywhere in the sample would read as Binary rather than LargeText —
    // so the initial write has to fill the whole sample window with non-NUL content
    // before the file is grown past it.
    WriteBytes(path, std::string(ProbeSampleSize, 'x'));

#ifdef CORE_PLATFORM_WINDOWS
    // Marking the file sparse first means growing it reserves no real disk clusters,
    // regardless of how much free space the volume actually has.
    if (!MarkSparse(path))
    {
        return;
    }
#endif

    std::error_code resizeError;
    std::filesystem::resize_file(path, LargeFileThreshold + 1, resizeError);
    REQUIRE_FALSE(resizeError);

    DocumentSet documents;

    CHECK(documents.Open(path, true) == OpenOutcome::NeedsConfirmation);
    CHECK(documents.Count() == 0);

    CHECK(documents.OpenConfirmed(path, true) == OpenOutcome::Opened);
    CHECK(documents.Count() == 1);
    CHECK(documents.At(0).document.has_value());
}

TEST_CASE("The failure message names the size, so the number is not a mystery")
{
    OpenItem item;
    item.failure = OpenFailure::TooLarge;
    item.size = 780ull * 1024 * 1024;

    CHECK(FailureMessage(item).find("780") != std::string::npos);
}

TEST_CASE("Closing removes the item and moves the active one")
{
    TempDirectory directory;
    const auto first = directory.Path() / "a.txt";
    const auto second = directory.Path() / "b.txt";
    WriteBytes(first, "a");
    WriteBytes(second, "b");

    DocumentSet documents;
    documents.Open(first, true);
    documents.Open(second, true);

    documents.Close(1);

    CHECK(documents.Count() == 1);
    CHECK(documents.Active() == std::optional<std::size_t>{0});
}

TEST_CASE("Closing the last item leaves nothing active")
{
    TempDirectory directory;
    const auto path = directory.Path() / "a.txt";
    WriteBytes(path, "a");

    DocumentSet documents;
    documents.Open(path, true);
    documents.Close(0);

    CHECK(documents.Count() == 0);
    CHECK_FALSE(documents.Active().has_value());
}

TEST_CASE("Closing an item before the active one keeps the same item active")
{
    TempDirectory directory;
    const auto first = directory.Path() / "a.txt";
    const auto second = directory.Path() / "b.txt";
    WriteBytes(first, "a");
    WriteBytes(second, "b");

    DocumentSet documents;
    documents.Open(first, true);
    documents.Open(second, true);
    REQUIRE(documents.Active() == std::optional<std::size_t>{1});

    documents.Close(0);

    // "b.txt" was active and still is, at its new index.
    REQUIRE(documents.Count() == 1);
    CHECK(documents.Active() == std::optional<std::size_t>{0});
    CHECK(documents.At(0).path == second);
}

TEST_CASE("Closing everything empties the set")
{
    TempDirectory directory;
    const auto first = directory.Path() / "a.txt";
    const auto second = directory.Path() / "b.txt";
    WriteBytes(first, "a");
    WriteBytes(second, "b");

    DocumentSet documents;
    documents.Open(first, true);
    documents.Open(second, true);

    documents.CloseAll();

    CHECK(documents.Count() == 0);
    CHECK_FALSE(documents.Active().has_value());
}

TEST_CASE("A file can be found by path")
{
    TempDirectory directory;
    const auto path = directory.Path() / "a.txt";
    WriteBytes(path, "a");

    DocumentSet documents;
    documents.Open(path, true);

    CHECK(documents.Find(path) == std::optional<std::size_t>{0});
    CHECK_FALSE(documents.Find(directory.Path() / "other.txt").has_value());
}

TEST_CASE("Each item carries its own caret, so switching away and back keeps the place")
{
    TempDirectory directory;
    const auto first = directory.Path() / "a.txt";
    const auto second = directory.Path() / "b.txt";
    WriteBytes(first, "hello");
    WriteBytes(second, "world");

    DocumentSet documents;
    documents.Open(first, true);
    documents.At(0).caret.offset = 3;
    documents.Open(second, true);

    documents.Activate(0);

    CHECK(documents.At(0).caret.offset == 3);
    CHECK(documents.At(1).caret.offset == 0);
}

TEST_CASE("A single click opens a preview and the next one replaces it")
{
    TempDirectory directory;
    const auto first = directory.Path() / "a.txt";
    const auto second = directory.Path() / "b.txt";
    WriteBytes(first, "a");
    WriteBytes(second, "b");

    DocumentSet documents;

    documents.Open(first, false);
    REQUIRE(documents.Count() == 1);
    CHECK(documents.Preview() == std::optional<std::size_t>{0});

    documents.Open(second, false);
    CHECK(documents.Count() == 1);
    CHECK(documents.At(0).path == second);
    CHECK(documents.Preview() == std::optional<std::size_t>{0});
}

TEST_CASE("A double click opens something that does not get replaced")
{
    TempDirectory directory;
    const auto first = directory.Path() / "a.txt";
    const auto second = directory.Path() / "b.txt";
    WriteBytes(first, "a");
    WriteBytes(second, "b");

    DocumentSet documents;
    documents.Open(first, true);
    documents.Open(second, false);

    CHECK(documents.Count() == 2);
    CHECK_FALSE(documents.At(0).preview);
    CHECK(documents.At(1).preview);
}

TEST_CASE("Promoting a preview frees the slot")
{
    TempDirectory directory;
    const auto first = directory.Path() / "a.txt";
    const auto second = directory.Path() / "b.txt";
    WriteBytes(first, "a");
    WriteBytes(second, "b");

    DocumentSet documents;
    documents.Open(first, false);
    documents.Promote(0);

    CHECK_FALSE(documents.Preview().has_value());

    documents.Open(second, false);

    CHECK(documents.Count() == 2);
}

TEST_CASE("Opening an already-open preview permanently promotes it")
{
    TempDirectory directory;
    const auto path = directory.Path() / "a.txt";
    WriteBytes(path, "a");

    DocumentSet documents;
    documents.Open(path, false);
    REQUIRE(documents.At(0).preview);

    documents.Open(path, true);

    CHECK_FALSE(documents.At(0).preview);
    CHECK_FALSE(documents.Preview().has_value());
}

TEST_CASE("Closing the preview clears the slot")
{
    TempDirectory directory;
    const auto path = directory.Path() / "a.txt";
    WriteBytes(path, "a");

    DocumentSet documents;
    documents.Open(path, false);
    documents.Close(0);

    CHECK_FALSE(documents.Preview().has_value());
}

TEST_CASE("A title is the file name when nothing collides with it")
{
    TempDirectory directory;
    const auto path = directory.Path() / "main.cpp";
    WriteBytes(path, "a");

    DocumentSet documents;
    documents.Open(path, true);

    CHECK(documents.DisplayTitle(0) == "main.cpp");
}

TEST_CASE("Two files of the same name both gain their parent directory")
{
    TempDirectory directory;
    std::filesystem::create_directories(directory.Path() / "Core");
    std::filesystem::create_directories(directory.Path() / "Graphite");

    const auto first = directory.Path() / "Core" / "main.cpp";
    const auto second = directory.Path() / "Graphite" / "main.cpp";
    WriteBytes(first, "a");
    WriteBytes(second, "b");

    DocumentSet documents;
    documents.Open(first, true);
    documents.Open(second, true);

    CHECK(documents.DisplayTitle(0) == "Core/main.cpp");
    CHECK(documents.DisplayTitle(1) == "Graphite/main.cpp");
}

TEST_CASE("A third file with a different name is not dragged into the disambiguation")
{
    TempDirectory directory;
    std::filesystem::create_directories(directory.Path() / "Core");

    const auto first = directory.Path() / "Core" / "main.cpp";
    const auto second = directory.Path() / "main.cpp";
    const auto third = directory.Path() / "other.cpp";
    WriteBytes(first, "a");
    WriteBytes(second, "b");
    WriteBytes(third, "c");

    DocumentSet documents;
    documents.Open(first, true);
    documents.Open(second, true);
    documents.Open(third, true);

    CHECK(documents.DisplayTitle(2) == "other.cpp");
}

TEST_CASE("Paths follow the workspace root when it moves")
{
    TempDirectory directory;
    const auto path = directory.Path() / "a.txt";
    WriteBytes(path, "a");

    DocumentSet documents;
    documents.Open(path, true);

    documents.RewritePaths(directory.Path(), "D:/moved");

    CHECK(documents.At(0).path == std::filesystem::path{"D:/moved"} / "a.txt");
}

TEST_CASE("A path outside the moved root is left alone")
{
    TempDirectory directory;
    const auto path = directory.Path() / "a.txt";
    WriteBytes(path, "a");

    DocumentSet documents;
    documents.Open(path, true);

    documents.RewritePaths("D:/somewhere-else", "D:/moved");

    CHECK(documents.At(0).path == path);
}

TEST_CASE("A path equal to the moved root is rewritten without a trailing dot")
{
    TempDirectory directory;

    DocumentSet documents;

    // A directory path is a legitimate OpenItem::path: opening a directory produces a
    // ReadFailed item at exactly that path, and the workspace root itself can be that
    // path when the item under test is the root.
    documents.Open(directory.Path(), true);
    REQUIRE(documents.Count() == 1);

    documents.RewritePaths(directory.Path(), "D:/moved");

    CHECK(documents.At(0).path == std::filesystem::path{"D:/moved"});
}

TEST_CASE("A deleted file keeps its window and its content")
{
    TempDirectory directory;
    const auto path = directory.Path() / "a.txt";
    WriteBytes(path, "still here");

    DocumentSet documents;
    documents.Open(path, true);

    documents.MarkMissing(path);

    REQUIRE(documents.Count() == 1);
    CHECK(documents.At(0).failure == OpenFailure::Missing);
    REQUIRE(documents.At(0).document.has_value());
    CHECK(documents.At(0).document->Buffer().Text() == "still here");
}

TEST_CASE("Marking a file that is not open does nothing")
{
    DocumentSet documents;

    documents.MarkMissing("D:/never-opened.txt");

    CHECK(documents.Count() == 0);
}

TEST_CASE("Marking a file missing clears any detail an earlier failure left behind")
{
    TempDirectory directory;
    const auto path = directory.Path() / "sub";
    std::filesystem::create_directories(path);

    DocumentSet documents;

    // Opening a directory fails with a specific detail from ProbeFile rather than the
    // generic Missing message.
    documents.Open(path, true);

    REQUIRE(documents.Count() == 1);
    REQUIRE(documents.At(0).failure == OpenFailure::ReadFailed);
    REQUIRE_FALSE(documents.At(0).detail.empty());

    documents.MarkMissing(path);

    CHECK(documents.At(0).failure == OpenFailure::Missing);
    CHECK(documents.At(0).detail.empty());
    CHECK(FailureMessage(documents.At(0)).find("exist") != std::string::npos);
}

TEST_CASE("A single click that needs confirmation does not evict the preview")
{
    TempDirectory directory;
    const auto path = directory.Path() / "a.txt";
    WriteBytes(path, "a");

    const auto big = directory.Path() / "big.log";

    // See "A large file asks before it loads, and adds nothing until confirmed" for why
    // the sample window has to be filled with non-NUL bytes before growing the file.
    WriteBytes(big, std::string(ProbeSampleSize, 'x'));

#ifdef CORE_PLATFORM_WINDOWS
    if (!MarkSparse(big))
    {
        return;
    }
#endif

    std::error_code resizeError;
    std::filesystem::resize_file(big, LargeFileThreshold + 1, resizeError);
    REQUIRE_FALSE(resizeError);

    DocumentSet documents;
    documents.Open(path, false);
    REQUIRE(documents.Count() == 1);
    REQUIRE(documents.Preview() == std::optional<std::size_t>{0});

    CHECK(documents.Open(big, false) == OpenOutcome::NeedsConfirmation);

    CHECK(documents.Count() == 1);
    CHECK(documents.At(0).path == path);
    CHECK(documents.Preview() == std::optional<std::size_t>{0});
}

TEST_CASE("The document's own path follows the workspace root when it moves")
{
    TempDirectory directory;
    const auto path = directory.Path() / "a.txt";
    WriteBytes(path, "a");

    DocumentSet documents;
    documents.Open(path, true);
    REQUIRE(documents.At(0).document.has_value());

    documents.RewritePaths(directory.Path(), "D:/moved");

    // Two places hold this path and a save reads the document's own — so the two must
    // never be allowed to disagree.
    CHECK(documents.At(0).document->Path() == documents.At(0).path);
    CHECK(documents.At(0).document->Path() == std::filesystem::path{"D:/moved"} / "a.txt");
}

TEST_CASE("A deleted file says so in its tab title")
{
    TempDirectory directory;
    const auto path = directory.Path() / "a.txt";
    WriteBytes(path, "still here");

    DocumentSet documents;
    documents.Open(path, true);

    REQUIRE(documents.DisplayTitle(0) == "a.txt");

    documents.MarkMissing(path);

    CHECK(documents.DisplayTitle(0) == "a.txt (deleted)");
}

TEST_CASE("A deleted file that shares its name keeps the disambiguating directory too")
{
    TempDirectory directory;
    const auto first = directory.Path() / "one";
    const auto second = directory.Path() / "two";
    std::filesystem::create_directories(first);
    std::filesystem::create_directories(second);

    WriteBytes(first / "main.cpp", "a");
    WriteBytes(second / "main.cpp", "b");

    DocumentSet documents;
    documents.Open(first / "main.cpp", true);
    documents.Open(second / "main.cpp", true);

    documents.MarkMissing(first / "main.cpp");

    CHECK(documents.DisplayTitle(0) == "one/main.cpp (deleted)");
    CHECK(documents.DisplayTitle(1) == "two/main.cpp");
}

TEST_CASE("A missing file that comes back stops being marked deleted")
{
    TempDirectory directory;
    const auto path = directory.Path() / "a.txt";
    WriteBytes(path, "still here");

    DocumentSet documents;
    documents.Open(path, true);

    documents.MarkMissing(path);
    REQUIRE(documents.At(0).failure == OpenFailure::Missing);

    documents.MarkPresent(path);

    CHECK(documents.At(0).failure == OpenFailure::None);
    CHECK(documents.DisplayTitle(0).find("(deleted)") == std::string::npos);
}

TEST_CASE("MarkPresent leaves a failure that is not about the file being gone")
{
    TempDirectory directory;
    const auto path = directory.Path() / "binary.obj";
    WriteBytes(path, std::string("MZ\0\0binary", 10));

    DocumentSet documents;
    documents.Open(path, true);

    REQUIRE(documents.At(0).failure == OpenFailure::Binary);

    documents.MarkPresent(path);

    CHECK(documents.At(0).failure == OpenFailure::Binary);
}

TEST_CASE("Loading fills an item that opened before its file existed")
{
    TempDirectory directory;
    const auto path = directory.Path() / "appears.txt";

    DocumentSet documents;
    REQUIRE(documents.Open(path, true) == OpenOutcome::Failed);
    REQUIRE_FALSE(documents.At(0).document.has_value());

    WriteBytes(path, "here now\n");

    CHECK(LoadFromFile(documents.At(0), false) == ItemLoad::Loaded);
    REQUIRE(documents.At(0).document.has_value());
    CHECK(documents.At(0).document->Buffer().Text() == "here now\n");
    CHECK(documents.At(0).failure == OpenFailure::None);
    CHECK(documents.At(0).detail.empty());
}

TEST_CASE("Loading a file that turned out to be binary leaves the reason, not a document")
{
    TempDirectory directory;
    const auto path = directory.Path() / "appears.obj";

    DocumentSet documents;
    REQUIRE(documents.Open(path, true) == OpenOutcome::Failed);

    WriteBytes(path, std::string("MZ\0\0binary", 10));

    CHECK(LoadFromFile(documents.At(0), false) == ItemLoad::Failed);
    CHECK_FALSE(documents.At(0).document.has_value());
    CHECK(documents.At(0).failure == OpenFailure::Binary);
}

TEST_CASE("Loading a large file asks before reading it, and reads it once confirmed")
{
    TempDirectory directory;
    const auto path = directory.Path() / "appears.log";

    DocumentSet documents;
    REQUIRE(documents.Open(path, true) == OpenOutcome::Failed);

    // Same shape as the Open test above: the sample window has to be real text before
    // the file is grown past it, or the probe reads the zero fill as Binary.
    WriteBytes(path, std::string(ProbeSampleSize, 'x'));

#ifdef CORE_PLATFORM_WINDOWS
    if (!MarkSparse(path))
    {
        return;
    }
#endif

    std::error_code resizeError;
    std::filesystem::resize_file(path, LargeFileThreshold + 1, resizeError);
    REQUIRE_FALSE(resizeError);

    // Nothing is read and nothing is decided: the question belongs to whoever has a user
    // to ask, exactly as OpenOutcome::NeedsConfirmation works when the tab is created.
    CHECK(LoadFromFile(documents.At(0), false) == ItemLoad::NeedsConfirmation);
    CHECK_FALSE(documents.At(0).document.has_value());
    CHECK(documents.At(0).failure == OpenFailure::Missing);

    CHECK(LoadFromFile(documents.At(0), true) == ItemLoad::Loaded);
    CHECK(documents.At(0).document.has_value());
    CHECK(documents.At(0).failure == OpenFailure::None);
}
