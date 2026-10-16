#include <doctest/doctest.h>

#include "Core/Text/FileProbe.h"

#include "FileFixtures.h"
#include "SparseFileFixture.h"
#include "TempDirectory.h"

#include <filesystem>
#include <ostream>
#include <string>

using namespace Core;

namespace {
	std::string SampleWithNul(std::size_t length, std::size_t nulOffset)
	{
		std::string sample(length, 'a');

		if (nulOffset < length)
		{
			sample[nulOffset] = '\0';
		}

		return sample;
	}
}

TEST_CASE("An ordinary text file is text")
{
	CHECK(ClassifySample("int main() {}", 13).Kind == FileKind::Text);
	CHECK(ClassifySample("cz\xC4\x99\xC5\x9B\xC4\x87", 8).Kind == FileKind::Text);
}

TEST_CASE("An empty file is text")
{
	const FileProbe probe = ClassifySample("", 0);

	CHECK(probe.Kind == FileKind::Text);
	CHECK(probe.FileEncoding == Encoding::Utf8);
	CHECK(probe.Size == 0);
}

TEST_CASE("A NUL byte inside the sample means binary")
{
	const std::size_t nulOffsetInsideTheSample = 5000;
	const std::string sample = SampleWithNul(ProbeSampleSize, nulOffsetInsideTheSample);

	CHECK(ClassifySample(sample, ProbeSampleSize).Kind == FileKind::Binary);
}

TEST_CASE("A NUL byte past the sample goes unseen, which is the documented trade-off")
{
	TempDirectory directory;
	const std::size_t fileSize = 40000;
	const std::size_t nulOffsetPastTheSample = 20000;
	REQUIRE(nulOffsetPastTheSample >= ProbeSampleSize);

	std::string contents(fileSize, 'a');
	contents[nulOffsetPastTheSample] = '\0';
	const std::filesystem::path path = directory.GetPath() / "nul-past-the-sample.bin";
	WriteFileBytes(path, contents);

	const Result<FileProbe> probeResult = ProbeFile(path);

	REQUIRE(probeResult);
	CHECK(probeResult.GetValue().Kind == FileKind::Text);
}

TEST_CASE("A UTF-16 file is judged by its mark, not by the NUL bytes it is made of")
{
	std::string markedUtf16Hello = "\xFF\xFE";
	markedUtf16Hello += std::string("h\0e\0l\0l\0o\0", 10);

	REQUIRE(markedUtf16Hello.find('\0') != std::string::npos);

	const FileProbe probe = ClassifySample(markedUtf16Hello, markedUtf16Hello.size());

	CHECK(probe.Kind == FileKind::Text);
	CHECK(probe.FileEncoding == Encoding::Utf16Le);
}

TEST_CASE("A UTF-8 byte order mark is reported so the document can put it back")
{
	CHECK(ClassifySample("\xEF\xBB\xBFint main() {}", 16).FileEncoding == Encoding::Utf8Bom);
}

TEST_CASE("The size thresholds are checked from both sides")
{
	CHECK(ClassifySample("a", LargeFileThreshold).Kind == FileKind::Text);
	CHECK(ClassifySample("a", LargeFileThreshold + 1).Kind == FileKind::LargeText);

	CHECK(ClassifySample("a", MaximumFileSize).Kind == FileKind::LargeText);
	CHECK(ClassifySample("a", MaximumFileSize + 1).Kind == FileKind::TooLarge);
}

TEST_CASE("A file too large to load is still reported with its size")
{
	const FileProbe probe = ClassifySample("a", MaximumFileSize + 1);

	CHECK(probe.Kind == FileKind::TooLarge);
	CHECK(probe.Size == MaximumFileSize + 1);
}

TEST_CASE("Probing reads the file from disk")
{
	TempDirectory directory;
	const std::filesystem::path path = directory.GetPath() / "hello.txt";
	WriteFileBytes(path, "int main() {}");

	const Result<FileProbe> probe = ProbeFile(path);

	REQUIRE(probe);
	CHECK(probe.GetValue().Kind == FileKind::Text);
	CHECK(probe.GetValue().Size == 13);
}

TEST_CASE("Probing a file that is not there fails rather than guessing")
{
	TempDirectory directory;

	const Result<FileProbe> probe = ProbeFile(directory.GetPath() / "absent.txt");

	REQUIRE_FALSE(probe);
	CHECK(probe.GetError().Code == ErrorCode::NotFound);
}

TEST_CASE("Probing a directory fails")
{
	TempDirectory directory;

	const Result<FileProbe> probe = ProbeFile(directory.GetPath());

	REQUIRE_FALSE(probe);
}

TEST_CASE("UTF-16 without a mark is text, not binary")
{
	const std::string littleEndian = std::string("H\0e\0l\0l\0o\0", 10);
	const std::string bigEndian = std::string("\0H\0e\0l\0l\0o", 10);

	const FileProbe littleEndianProbe = ClassifySample(littleEndian, littleEndian.size());
	const FileProbe bigEndianProbe = ClassifySample(bigEndian, bigEndian.size());

	CHECK(littleEndianProbe.Kind == FileKind::Text);
	CHECK(littleEndianProbe.FileEncoding == Encoding::Utf16Le);
	CHECK(bigEndianProbe.Kind == FileKind::Text);
	CHECK(bigEndianProbe.FileEncoding == Encoding::Utf16Be);
}

TEST_CASE("Binary data is still binary even when its NUL bytes line up")
{
	const std::string bytes = std::string("\x01\0\x02\0\x03\0\x04\0", 8);

	CHECK(ClassifySample(bytes, bytes.size()).Kind == FileKind::Binary);
}

TEST_CASE("A large UTF-16 file is judged by its size like any other text")
{
	const std::string bytes = std::string("H\0i\0", 4);

	CHECK(ClassifySample(bytes, LargeFileThreshold + 1).Kind == FileKind::LargeText);
}

TEST_CASE("ReadDiskText drops a byte order mark, UTF-8 or UTF-16, as opening the file does, but refuses what is not text")
{
	TempDirectory directory;

	const DiskText marked = ReadDiskText(directory.MakeFile("marked.txt", "\xEF\xBB\xBFint x;"));
	CHECK(marked.Outcome == DiskTextOutcome::Read);
	CHECK(marked.Text == "int x;");

	const DiskText wide = ReadDiskText(directory.MakeFile("wide.txt", std::string{"\xFF\xFEh\0i\0", 6}));
	CHECK(wide.Outcome == DiskTextOutcome::Read);
	CHECK(wide.Text == "hi");

	const DiskText empty = ReadDiskText(directory.MakeFile("empty.txt"));
	CHECK(empty.Outcome == DiskTextOutcome::Read);
	CHECK(empty.Text.empty());

	CHECK(ReadDiskText(directory.MakeFile("object.obj", std::string{"MZ\0\0binary", 10})).Outcome ==
		  DiskTextOutcome::Binary);
	CHECK(ReadDiskText(directory.GetPath() / "absent.txt").Outcome == DiskTextOutcome::Unreadable);
	CHECK(ReadDiskText(directory.MakeDirectory("folder")).Outcome == DiskTextOutcome::Unreadable);

	const std::string markedUtf16WithUnpairedHighSurrogate{"\xFF\xFE"
														   "a\0"
														   "\x00\xD8"
														   "b\0",
														   8};
	CHECK(ReadDiskText(directory.MakeFile("damaged.txt", markedUtf16WithUnpairedHighSurrogate)).Outcome ==
		  DiskTextOutcome::Undecodable);
}

TEST_CASE("ReadDiskText ends its lines with LF, as opening the file does, so a column found in it is the column in the document")
{
	TempDirectory directory;
	CHECK(ReadDiskText(directory.MakeFile("crlf.txt", "a\r\nb")).Text == "a\nb");
}

TEST_CASE("ReadDiskText reads a file large enough to need confirmation rather than refusing it")
{
	TempDirectory directory;
	const std::filesystem::path path = directory.GetPath() / "big.log";

	if (!TryWriteLargeTextFile(path, LargeFileThreshold + 1))
	{
		return;
	}

	const DiskText largeFileText = ReadDiskText(path);

	CHECK(largeFileText.Outcome == DiskTextOutcome::Read);
	CHECK(largeFileText.Text.size() == LargeFileThreshold + 1);
}

TEST_CASE("ReadDiskText refuses a file past the maximum size")
{
	TempDirectory directory;
	const std::filesystem::path path = directory.GetPath() / "huge.log";

	if (!TryWriteLargeTextFile(path, MaximumFileSize + 1))
	{
		return;
	}

	CHECK(ReadDiskText(path).Outcome == DiskTextOutcome::TooLarge);
}
