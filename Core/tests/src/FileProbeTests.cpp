#include <doctest/doctest.h>

#include "Core/Text/FileProbe.h"

#include "TempDirectory.h"

#include <fstream>
#include <ostream>
#include <string>

using namespace Core;

namespace
{
    /// A sample of `length` printable bytes with a NUL dropped in at `nulAt`.
    std::string SampleWithNul(std::size_t length, std::size_t nulAt)
    {
        std::string sample(length, 'a');

        if (nulAt < length)
            sample[nulAt] = '\0';

        return sample;
    }

    void WriteFile(const std::filesystem::path& path, std::string_view contents)
    {
        std::ofstream stream{path, std::ios::binary};
        stream.write(contents.data(), static_cast<std::streamsize>(contents.size()));
    }
}

TEST_CASE("An ordinary text file is text")
{
    CHECK(ClassifySample("int main() {}", 13).kind == FileKind::Text);
    CHECK(ClassifySample("cz\xC4\x99\xC5\x9B\xC4\x87", 8).kind == FileKind::Text);
}

TEST_CASE("An empty file is text")
{
    const FileProbe probe = ClassifySample("", 0);

    CHECK(probe.kind == FileKind::Text);
    CHECK(probe.encoding == Encoding::Utf8);
    CHECK(probe.size == 0);
}

TEST_CASE("A NUL byte inside the sample means binary")
{
    const std::string sample = SampleWithNul(8192, 5000);

    CHECK(ClassifySample(sample, 8192).kind == FileKind::Binary);
}

TEST_CASE("A NUL byte past the sample goes unseen, which is the documented trade-off")
{
    // The file is 40 KB with a NUL at 20 000, but only the first 8 KB were read.
    const std::string sample = SampleWithNul(8192, 20000);

    CHECK(ClassifySample(sample, 40000).kind == FileKind::Text);
}

TEST_CASE("A UTF-16 file is judged by its mark, not by the NUL bytes it is made of")
{
    // Every other byte is NUL, so the order of the checks is the only thing keeping
    // this from being called binary.
    std::string sample = "\xFF\xFE";
    sample += std::string("h\0e\0l\0l\0o\0", 10);

    const FileProbe probe = ClassifySample(sample, sample.size());

    CHECK(probe.kind == FileKind::Text);
    CHECK(probe.encoding == Encoding::Utf16Le);
}

TEST_CASE("A UTF-8 byte order mark is reported so the document can put it back")
{
    CHECK(ClassifySample("\xEF\xBB\xBFint main() {}", 16).encoding == Encoding::Utf8Bom);
}

TEST_CASE("The size thresholds are checked from both sides")
{
    CHECK(ClassifySample("a", LargeFileThreshold).kind == FileKind::Text);
    CHECK(ClassifySample("a", LargeFileThreshold + 1).kind == FileKind::LargeText);

    CHECK(ClassifySample("a", MaximumFileSize).kind == FileKind::LargeText);
    CHECK(ClassifySample("a", MaximumFileSize + 1).kind == FileKind::TooLarge);
}

TEST_CASE("A file too large to load is still reported with its size")
{
    const FileProbe probe = ClassifySample("a", MaximumFileSize + 1);

    CHECK(probe.kind == FileKind::TooLarge);
    CHECK(probe.size == MaximumFileSize + 1);
}

TEST_CASE("Probing reads the file from disk")
{
    TempDirectory directory;
    const auto path = directory.Path() / "hello.txt";
    WriteFile(path, "int main() {}");

    const auto probe = ProbeFile(path);

    REQUIRE(probe);
    CHECK(probe.Value().kind == FileKind::Text);
    CHECK(probe.Value().size == 13);
}

TEST_CASE("Probing a file that is not there fails rather than guessing")
{
    TempDirectory directory;

    const auto probe = ProbeFile(directory.Path() / "absent.txt");

    REQUIRE_FALSE(probe);
    CHECK(probe.GetError().code == Core::ErrorCode::NotFound);
}

TEST_CASE("Probing a directory fails")
{
    TempDirectory directory;

    const auto probe = ProbeFile(directory.Path());

    REQUIRE_FALSE(probe);
}

TEST_CASE("UTF-16 without a mark is text, not binary")
{
    const std::string le = std::string("H\0e\0l\0l\0o\0", 10);
    const std::string be = std::string("\0H\0e\0l\0l\0o", 10);

    const FileProbe leProbe = ClassifySample(le, le.size());
    const FileProbe beProbe = ClassifySample(be, be.size());

    CHECK(leProbe.kind == FileKind::Text);
    CHECK(leProbe.encoding == Encoding::Utf16Le);
    CHECK(beProbe.kind == FileKind::Text);
    CHECK(beProbe.encoding == Encoding::Utf16Be);
}

TEST_CASE("Binary data is still binary even when its NUL bytes line up")
{
    const std::string bytes = std::string("\x01\0\x02\0\x03\0\x04\0", 8);

    CHECK(ClassifySample(bytes, bytes.size()).kind == FileKind::Binary);
}

TEST_CASE("A large UTF-16 file is judged by its size like any other text")
{
    const std::string bytes = std::string("H\0i\0", 4);

    CHECK(ClassifySample(bytes, LargeFileThreshold + 1).kind == FileKind::LargeText);
}
