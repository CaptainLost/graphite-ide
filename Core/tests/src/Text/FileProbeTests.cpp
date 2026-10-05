#include <doctest/doctest.h>

#include "Core/Text/FileProbe.h"

#include "TempDirectory.h"

#include <fstream>
#include <ostream>
#include <string>

using namespace Core;

namespace {
	std::string SampleWithNul(std::size_t length, std::size_t nulAt)
	{
		std::string sample(length, 'a');

		if (nulAt < length)
		{
			sample[nulAt] = '\0';
		}

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
	const std::string sample = SampleWithNul(8192, 5000);

	CHECK(ClassifySample(sample, 8192).Kind == FileKind::Binary);
}

TEST_CASE("A NUL byte past the sample goes unseen, which is the documented trade-off")
{
	// The file is 40 KB with a NUL at 20 000, but only the first 8 KB were read.
	const std::string sample = SampleWithNul(8192, 20000);

	CHECK(ClassifySample(sample, 40000).Kind == FileKind::Text);
}

TEST_CASE("A UTF-16 file is judged by its mark, not by the NUL bytes it is made of")
{
	// Every other byte is NUL, so the order of the checks is the only thing keeping
	// this from being called binary.
	std::string sample = "\xFF\xFE";
	sample += std::string("h\0e\0l\0l\0o\0", 10);

	const FileProbe probe = ClassifySample(sample, sample.size());

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
	const auto path = directory.GetPath() / "hello.txt";
	WriteFile(path, "int main() {}");

	const auto probe = ProbeFile(path);

	REQUIRE(probe);
	CHECK(probe.GetValue().Kind == FileKind::Text);
	CHECK(probe.GetValue().Size == 13);
}

TEST_CASE("Probing a file that is not there fails rather than guessing")
{
	TempDirectory directory;

	const auto probe = ProbeFile(directory.GetPath() / "absent.txt");

	REQUIRE_FALSE(probe);
	CHECK(probe.GetError().Code == Core::ErrorCode::NotFound);
}

TEST_CASE("Probing a directory fails")
{
	TempDirectory directory;

	const auto probe = ProbeFile(directory.GetPath());

	REQUIRE_FALSE(probe);
}

TEST_CASE("UTF-16 without a mark is text, not binary")
{
	const std::string le = std::string("H\0e\0l\0l\0o\0", 10);
	const std::string be = std::string("\0H\0e\0l\0l\0o", 10);

	const FileProbe leProbe = ClassifySample(le, le.size());
	const FileProbe beProbe = ClassifySample(be, be.size());

	CHECK(leProbe.Kind == FileKind::Text);
	CHECK(leProbe.FileEncoding == Encoding::Utf16Le);
	CHECK(beProbe.Kind == FileKind::Text);
	CHECK(beProbe.FileEncoding == Encoding::Utf16Be);
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

TEST_CASE("ReadDiskText keeps the byte order mark and refuses what is not text")
{
	TempDirectory directory;

	const DiskText marked = ReadDiskText(directory.MakeFile("marked.txt", "\xEF\xBB\xBFint x;"));
	CHECK(marked.Outcome == DiskTextOutcome::Read);
	CHECK(marked.Text == "\xEF\xBB\xBFint x;");

	const DiskText wide = ReadDiskText(directory.MakeFile("wide.txt", std::string{"\xFF\xFEh\0i\0", 6}));
	CHECK(wide.Outcome == DiskTextOutcome::Read);
	CHECK(wide.Text == "\xEF\xBB\xBFhi");

	const DiskText empty = ReadDiskText(directory.MakeFile("empty.txt"));
	CHECK(empty.Outcome == DiskTextOutcome::Read);
	CHECK(empty.Text.empty());

	CHECK(ReadDiskText(directory.MakeFile("object.obj", std::string{"MZ\0\0binary", 10})).Outcome ==
		  DiskTextOutcome::Binary);
	CHECK(ReadDiskText(directory.GetPath() / "absent.txt").Outcome == DiskTextOutcome::Unreadable);
	CHECK(ReadDiskText(directory.MakeDir("folder")).Outcome == DiskTextOutcome::Unreadable);

	const std::string markedUtf16WithUnpairedHighSurrogate{"\xFF\xFE"
														   "a\0"
														   "\x00\xD8"
														   "b\0",
														   8};
	CHECK(ReadDiskText(directory.MakeFile("damaged.txt", markedUtf16WithUnpairedHighSurrogate)).Outcome ==
		  DiskTextOutcome::Undecodable);
}

TEST_CASE("ReadDiskText leaves line endings as they are on disk")
{
	TempDirectory directory;
	CHECK(ReadDiskText(directory.MakeFile("crlf.txt", "a\r\nb")).Text == "a\r\nb");
}
