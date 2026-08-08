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

using namespace core::text;

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

TEST_CASE("A UTF-16 file is refused by name, not called binary")
{
    TempDirectory directory;
    const auto path = directory.Path() / "utf16.txt";
    WriteBytes(path, std::string("\xFF\xFEh\0e\0l\0l\0o\0", 12));

    DocumentSet documents;

    CHECK(documents.Open(path, true) == OpenOutcome::Failed);
    REQUIRE(documents.Count() == 1);
    CHECK(documents.At(0).failure == OpenFailure::UnsupportedEncoding);
    CHECK(documents.At(0).encoding == Encoding::Utf16Le);
    CHECK(FailureMessage(documents.At(0)).find("UTF-16") != std::string::npos);
}

TEST_CASE("A file that is not there opens as an item saying so, not as silence")
{
    TempDirectory directory;

    DocumentSet documents;

    CHECK(documents.Open(directory.Path() / "absent.txt", true) == OpenOutcome::Failed);
    REQUIRE(documents.Count() == 1);
    CHECK(documents.At(0).failure == OpenFailure::Missing);
}

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
