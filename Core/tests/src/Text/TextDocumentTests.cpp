#include <doctest/doctest.h>

#include "Core/FileSystem/FileSystem.h"
#include "Core/Text/FileProbe.h"
#include "Core/Text/TextDocument.h"

#include "FileFixtures.h"
#include "SparseFileFixture.h"
#include "TempDirectory.h"

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <ostream>
#include <string>
#include <system_error>
#include <vector>

#ifdef CORE_PLATFORM_WINDOWS
#include <windows.h>
#endif

using namespace Core;

namespace {
	TextDocument DocumentFromBytes(std::string bytes)
	{
		Result<TextDocument> document = TextDocument::FromContent(std::move(bytes), "C:/tmp/sample.txt");
		REQUIRE(document);

		return std::move(document).GetValue();
	}

	std::string ReadAllBytes(const std::filesystem::path& path)
	{
		std::ifstream stream{path, std::ios::binary};
		std::string content{std::istreambuf_iterator<char>{stream}, std::istreambuf_iterator<char>{}};
		return content;
	}

	std::filesystem::path MakeTempFile(const std::string& contents)
	{
		static TempDirectory directory;
		static std::atomic<int> filesMade{0};

		const std::string fileName = "doc-" + std::to_string(filesMade.fetch_add(1)) + ".tmp";
		return directory.MakeFile(fileName, contents);
	}
}

TEST_CASE("A plain LF file loads as it is")
{
	const TextDocument document = DocumentFromBytes("one\ntwo\n");

	CHECK(document.GetBuffer().Materialize() == "one\ntwo\n");
	CHECK(document.GetDocumentLineEnding() == LineEnding::Lf);
	CHECK_FALSE(document.HasMixedLineEndings());
	CHECK(document.GetFileEncoding() == Encoding::Utf8);
	CHECK_FALSE(document.IsReadOnly());
}

TEST_CASE("CRLF is normalised to LF inside the buffer so nothing downstream has to know CRLF exists")
{
	const TextDocument document = DocumentFromBytes("one\r\ntwo\r\n");

	CHECK(document.GetBuffer().Materialize() == "one\ntwo\n");
	CHECK(document.GetBuffer().GetLineCount() == 3);
	CHECK(document.GetDocumentLineEnding() == LineEnding::Crlf);
}

TEST_CASE("A CRLF file is written back as CRLF")
{
	const TextDocument document = DocumentFromBytes("one\r\ntwo\r\n");

	CHECK(document.EncodeToBytes() == "one\r\ntwo\r\n");
}

TEST_CASE("An untouched file comes back byte for byte")
{
	const std::string original = "int main()\r\n{\r\n    return 0;\r\n}\r\n";

	CHECK(DocumentFromBytes(original).EncodeToBytes() == original);
}

TEST_CASE("A file with both styles takes the majority and says it was mixed before saving unifies it")
{
	const TextDocument document = DocumentFromBytes("a\r\nb\r\nc\r\nd\ne\r\n");

	CHECK(document.GetDocumentLineEnding() == LineEnding::Crlf);
	CHECK(document.HasMixedLineEndings());

	CHECK(document.EncodeToBytes() == "a\r\nb\r\nc\r\nd\r\ne\r\n");
}

TEST_CASE("A mostly-LF file with one CRLF takes LF")
{
	const TextDocument document = DocumentFromBytes("a\nb\nc\r\nd\n");

	CHECK(document.GetDocumentLineEnding() == LineEnding::Lf);
	CHECK(document.HasMixedLineEndings());
}

TEST_CASE("A file with no line break at all is LF")
{
	const TextDocument document = DocumentFromBytes("no newline here");

	CHECK(document.GetDocumentLineEnding() == LineEnding::Lf);
	CHECK_FALSE(document.HasMixedLineEndings());
	CHECK(document.EncodeToBytes() == "no newline here");
}

TEST_CASE("An empty file loads as one empty line")
{
	const TextDocument document = DocumentFromBytes("");

	CHECK(document.GetBuffer().GetLineCount() == 1);
	CHECK(document.EncodeToBytes() == "");
}

TEST_CASE("A file of nothing but newlines keeps every one of them")
{
	const TextDocument document = DocumentFromBytes("\n\n\n");

	CHECK(document.GetBuffer().GetLineCount() == 4);
	CHECK(document.EncodeToBytes() == "\n\n\n");
}

TEST_CASE("A lone carriage return next to a real CRLF round-trips untouched")
{
	CHECK(DocumentFromBytes("a\r\r\nb").EncodeToBytes() == "a\r\r\nb");
}

TEST_CASE("A UTF-8 byte order mark is kept out of the content where it would throw off every column of line 1 and is put back on save")
{
	const TextDocument document = DocumentFromBytes("\xEF\xBB\xBFint main() {}");

	CHECK(document.GetBuffer().Materialize() == "int main() {}");
	CHECK(document.GetFileEncoding() == Encoding::Utf8Bom);
	CHECK(document.EncodeToBytes() == "\xEF\xBB\xBFint main() {}");
}

TEST_CASE("A Visual Studio file with a mark and CRLF round-trips exactly")
{
	const std::string original = "\xEF\xBB\xBF<Project>\r\n</Project>\r\n";

	CHECK(DocumentFromBytes(original).EncodeToBytes() == original);
}

TEST_CASE("Content that is not valid UTF-8 loads read-only with its bytes untouched")
{
	const std::string windows1250Text = "cz\xEA\x9C\xE6\r\nnext line\r\n";
	const TextDocument document = DocumentFromBytes(windows1250Text);

	CHECK(document.IsReadOnly());
	CHECK(document.GetFileEncoding() == Encoding::Unknown);
	CHECK(document.EncodeToBytes() == windows1250Text);
}

TEST_CASE("The document remembers where it came from")
{
	const TextDocument document = DocumentFromBytes("hello");

	CHECK(document.GetPath() == std::filesystem::path{"C:/tmp/sample.txt"});
}

TEST_CASE("A freshly loaded document is not modified")
{
	const TextDocument document = DocumentFromBytes("hello");

	CHECK_FALSE(document.IsModified());
}

TEST_CASE("Editing modifies, and undoing all the way back clears it again")
{
	constexpr std::size_t kEndOfHello = 5;

	TextDocument document = DocumentFromBytes("hello");

	document.GetBuffer().Insert(kEndOfHello, " world");
	CHECK(document.IsModified());

	document.GetBuffer().Undo();
	CHECK_FALSE(document.IsModified());
}

TEST_CASE("Marking saved moves the point that modification is measured from")
{
	constexpr std::size_t kEndOfHello = 5;

	TextDocument document = DocumentFromBytes("hello");

	document.GetBuffer().Insert(kEndOfHello, "!");
	document.MarkSaved();
	CHECK_FALSE(document.IsModified());

	document.GetBuffer().Undo();
	CHECK(document.IsModified());

	document.GetBuffer().Redo();
	CHECK_FALSE(document.IsModified());
}

TEST_CASE("Edits made after saving are written back with the file's own line ending")
{
	TextDocument document = DocumentFromBytes("one\r\ntwo\r\n");

	document.GetBuffer().Insert(document.GetBuffer().GetLength(), "three\n");

	CHECK(document.EncodeToBytes() == "one\r\ntwo\r\nthree\r\n");
}

TEST_CASE("A byte order mark followed by invalid UTF-8 still comes back from EncodeToBytes so writing a read-only document does not shrink it")
{
	const std::string markThenWindows1250Text = "\xEF\xBB\xBF"
												"cz\xEA\x9C\xE6\r\nnext line\r\n";
	const TextDocument document = DocumentFromBytes(markThenWindows1250Text);

	CHECK(document.IsReadOnly());
	CHECK(document.GetFileEncoding() == Encoding::Unknown);
	CHECK(document.EncodeToBytes() == markThenWindows1250Text);
}

TEST_CASE("Load reads an ordinary file from disk")
{
	TempDirectory directory;
	const std::filesystem::path path = directory.MakeFile("sample.txt", "one\r\ntwo\r\n");

	const Result<TextDocument> document = TextDocument::Load(path);
	REQUIRE(document);

	CHECK(document.GetValue().GetBuffer().Materialize() == "one\ntwo\n");
	CHECK(document.GetValue().GetDocumentLineEnding() == LineEnding::Crlf);
}

TEST_CASE("Load succeeds on a legitimately empty file whose first read sets eof and fail but not bad")
{
	TempDirectory directory;
	const std::filesystem::path path = directory.MakeFile("empty.txt", "");

	const Result<TextDocument> document = TextDocument::Load(path);
	REQUIRE(document);

	CHECK(document.GetValue().GetBuffer().Materialize() == "");
	CHECK(document.GetValue().GetBuffer().GetLineCount() == 1);
}

TEST_CASE("A file on disk loads with its path and content")
{
	TempDirectory directory;
	const std::filesystem::path path = directory.GetPath() / "main.cpp";
	WriteFileBytes(path, "int main()\r\n{\r\n}\r\n");

	const Result<TextDocument> document = TextDocument::Load(path);

	REQUIRE(document);
	CHECK(document.GetValue().GetBuffer().Materialize() == "int main()\n{\n}\n");
	CHECK(document.GetValue().GetDocumentLineEnding() == LineEnding::Crlf);
	CHECK(document.GetValue().GetPath() == path);
}

TEST_CASE("A file read from disk is written back byte for byte")
{
	TempDirectory directory;
	const std::filesystem::path path = directory.GetPath() / "project.xml";
	const std::string original = "\xEF\xBB\xBF<Project>\r\n</Project>\r\n";
	WriteFileBytes(path, original);

	const Result<TextDocument> document = TextDocument::Load(path);

	REQUIRE(document);
	CHECK(document.GetValue().EncodeToBytes() == original);
}

TEST_CASE("Loading a file that is not there fails rather than giving an empty document")
{
	TempDirectory directory;

	const Result<TextDocument> document = TextDocument::Load(directory.GetPath() / "absent.cpp");

	REQUIRE_FALSE(document);
	CHECK(document.GetError().Code == Core::ErrorCode::NotFound);
}

TEST_CASE("A file whose name lies outside the active code page loads through the wide-character API")
{
	TempDirectory directory;
	const std::filesystem::path path = directory.GetPath() / Core::FromUtf8("\xC4\x87wiczenie.txt");
	WriteFileBytes(path, "content");

	const Result<TextDocument> document = TextDocument::Load(path);

	REQUIRE(document);
	CHECK(document.GetValue().GetBuffer().Materialize() == "content");
	CHECK(document.GetValue().GetPath() == path);
}

TEST_CASE("Loading a directory fails with InvalidArgument rather than NotFound")
{
	TempDirectory directory;

	const Result<TextDocument> document = TextDocument::Load(directory.GetPath());

	REQUIRE_FALSE(document);
	CHECK(document.GetError().Code == Core::ErrorCode::InvalidArgument);
}

#ifdef CORE_PLATFORM_WINDOWS
TEST_CASE("Load refuses a file larger than the maximum size, before reading it")
{
	TempDirectory directory;
	const std::filesystem::path path = directory.GetPath() / "huge.bin";
	WriteFileBytes(path, "");

	const bool markedSparse = MarkFileSparse(path);
	if (!markedSparse)
	{
		return;
	}

	// On a sparse file NTFS grows the size without writing a byte, so the real size check runs
	// against a real file without the test paying for a write of MaximumFileSize bytes.
	const std::uint64_t oversizedFileSize = MaximumFileSize + 1;
	std::error_code resizeError;
	std::filesystem::resize_file(path, oversizedFileSize, resizeError);
	REQUIRE_FALSE(resizeError);

	const Result<TextDocument> document = TextDocument::Load(path);

	REQUIRE_FALSE(document);
	CHECK(document.GetError().Code == Core::ErrorCode::InvalidArgument);
	CHECK(document.GetError().Message.find(std::to_string(oversizedFileSize)) != std::string::npos);
}
#else
TEST_CASE("Load refuses a file larger than the maximum size, before reading it" * doctest::skip())
{
}
#endif

#ifdef CORE_PLATFORM_WINDOWS
TEST_CASE("Load reports AccessDenied rather than NotFound for a file it cannot open")
{
	constexpr DWORD kShareWithNoOne = 0;

	TempDirectory directory;
	const std::filesystem::path path = directory.GetPath() / "locked.txt";
	WriteFileBytes(path, "content");

	const HANDLE exclusiveHandle = ::CreateFileW(path.c_str(), GENERIC_READ, kShareWithNoOne, nullptr,
												 OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
	REQUIRE(exclusiveHandle != INVALID_HANDLE_VALUE);

	const Result<TextDocument> document = TextDocument::Load(path);

	// Closed before any assertion can fail the test, so a locked file never survives into
	// TempDirectory's destructor and takes a later test down with it.
	::CloseHandle(exclusiveHandle);

	REQUIRE_FALSE(document);
	CHECK(document.GetError().Code == Core::ErrorCode::AccessDenied);
}
#else
TEST_CASE("Load reports AccessDenied rather than NotFound for a file it cannot open" * doctest::skip())
{
}
#endif

TEST_CASE("LF text keeps its line endings when LF is asked for")
{
	CHECK(WithLineEnding("one\ntwo\n", LineEnding::Lf) == "one\ntwo\n");
}

TEST_CASE("LF text becomes CRLF when CRLF is asked for")
{
	CHECK(WithLineEnding("one\ntwo\n", LineEnding::Crlf) == "one\r\ntwo\r\n");
}

TEST_CASE("Text with no line break is unchanged either way")
{
	CHECK(WithLineEnding("one", LineEnding::Lf) == "one");
	CHECK(WithLineEnding("one", LineEnding::Crlf) == "one");
}

TEST_CASE("An empty selection converts to nothing")
{
	CHECK(WithLineEnding("", LineEnding::Crlf).empty());
}

TEST_CASE("A blank line becomes its own CRLF rather than being swallowed")
{
	CHECK(WithLineEnding("\n\n", LineEnding::Crlf) == "\r\n\r\n");
}

TEST_CASE("A UTF-16 file with a mark opens and reports its encoding")
{
	const Result<TextDocument> document = TextDocument::FromContent(
		std::string("\xFF\xFEH\0i\0", 6), "C:/tmp/utf16.txt");

	REQUIRE(document);
	CHECK(document.GetValue().GetBuffer().Materialize() == "Hi");
	CHECK(document.GetValue().GetFileEncoding() == Encoding::Utf16Le);
	CHECK_FALSE(document.GetValue().IsReadOnly());
}

TEST_CASE("A UTF-16 file without a mark opens too")
{
	const Result<TextDocument> document = TextDocument::FromContent(
		std::string("H\0e\0l\0l\0o\0", 10), "C:/tmp/nobom.txt");

	REQUIRE(document);
	CHECK(document.GetValue().GetBuffer().Materialize() == "Hello");
	CHECK(document.GetValue().GetFileEncoding() == Encoding::Utf16Le);
}

TEST_CASE("A UTF-16 file comes back byte for byte, with and without its mark")
{
	const std::string withMark = std::string("\xFF\xFEH\0i\0", 6);
	const std::string withoutMark = std::string("H\0e\0l\0l\0o\0", 10);
	const std::string bigEndian = std::string("\xFE\xFF\0H\0i", 6);

	for (const std::string& original : {withMark, withoutMark, bigEndian})
	{
		const Result<TextDocument> document = TextDocument::FromContent(original, "C:/tmp/round.txt");

		REQUIRE(document);
		CHECK(document.GetValue().EncodeToBytes() == original);
	}
}

TEST_CASE("CRLF in a UTF-16 file is normalised and restored like any other file")
{
	const std::string original = std::string("a\0\r\0\n\0b\0", 8);
	const Result<TextDocument> document = TextDocument::FromContent(original, "C:/tmp/crlf.txt");

	REQUIRE(document);
	CHECK(document.GetValue().GetBuffer().Materialize() == "a\nb");
	CHECK(document.GetValue().GetDocumentLineEnding() == LineEnding::Crlf);
	CHECK(document.GetValue().EncodeToBytes() == original);
}

TEST_CASE("A damaged UTF-16 file is refused with a message naming the damage")
{
	const std::string markThenLoneLowSurrogate = std::string("\xFF\xFE\x42\xDE", 4);
	const Result<TextDocument> document = TextDocument::FromContent(markThenLoneLowSurrogate, "C:/tmp/damaged.txt");

	REQUIRE_FALSE(document);
	CHECK(document.GetError().Message.find("damaged") != std::string::npos);
	CHECK(document.GetError().Message.find("surrogate") != std::string::npos);
}

TEST_CASE("An odd byte count is refused as damage rather than opened short")
{
	const Result<TextDocument> document = TextDocument::FromContent(
		std::string("\xFF\xFEH\0i", 5), "C:/tmp/odd.txt");

	REQUIRE_FALSE(document);
	CHECK(document.GetError().Message.find("odd") != std::string::npos);
}

TEST_CASE("The probe and the document agree about a file that changes shape past the sample")
{
	constexpr std::size_t kTailCapacity = 8;

	std::string unmarkedUtf16;
	unmarkedUtf16.reserve(ProbeSampleSize + kTailCapacity);

	while (unmarkedUtf16.size() < ProbeSampleSize)
	{
		unmarkedUtf16 += std::string("a\0", 2);
	}

	REQUIRE(unmarkedUtf16.size() == ProbeSampleSize);

	const std::string formFeedJustPastTheSample = std::string("\x0C\0", 2);
	unmarkedUtf16 += formFeedJustPastTheSample;
	unmarkedUtf16 += std::string("b\0", 2);

	TempDirectory directory;
	const std::filesystem::path path = directory.GetPath() / "tail.txt";
	WriteFileBytes(path, unmarkedUtf16);

	const Result<FileProbe> probe = ProbeFile(path);

	REQUIRE(probe);
	CHECK(probe.GetValue().FileEncoding == Encoding::Utf16Le);
	CHECK(probe.GetValue().Kind == FileKind::Text);

	const Result<TextDocument> document = TextDocument::Load(path);

	REQUIRE_FALSE(document);
	CHECK(document.GetError().Code == Core::ErrorCode::InvalidArgument);
	CHECK(document.GetError().Message.find("UTF-16 LE") != std::string::npos);
}

TEST_CASE("A marked UTF-16 file keeps its control characters instead of being refused")
{
	// Each literal is split where a letter that is also a hex digit follows a \x escape, which
	// would otherwise swallow it.
	const std::string markedFormFeedAndC1Control = std::string("\xFF\xFE"
															   "a\0\x0C\0b\0\x85\0",
															   10);
	const std::string sameTextInUtf8 = std::string("a\x0C"
												   "b\xC2\x85",
												   5);

	const Result<TextDocument> document = TextDocument::FromContent(markedFormFeedAndC1Control, "C:/tmp/pagebreak.c");

	REQUIRE(document);
	CHECK(document.GetValue().GetFileEncoding() == Encoding::Utf16Le);
	CHECK_FALSE(document.GetValue().IsReadOnly());
	CHECK(document.GetValue().GetBuffer().Materialize() == sameTextInUtf8);
	CHECK(document.GetValue().EncodeToBytes() == markedFormFeedAndC1Control);

	CHECK_FALSE(DocumentFromBytes(sameTextInUtf8).IsReadOnly());
}

TEST_CASE("A UTF-32 file is refused for the NUL characters it decodes to as UTF-16 rather than opened")
{
	const std::string markedUtf32LeHi = std::string("\xFF\xFE\0\0H\0\0\0i\0\0\0", 12);
	const Result<TextDocument> document = TextDocument::FromContent(markedUtf32LeHi, "C:/tmp/utf32.txt");

	REQUIRE_FALSE(document);
	CHECK(document.GetError().Message.find("UTF-16 LE") != std::string::npos);
	CHECK(document.GetError().Message.find("NUL") != std::string::npos);
}

TEST_CASE("Saving round trips every encoding byte for byte")
{
	const std::vector<std::string> contents = {
		"line one\r\nline two\r\n",
		std::string{"\xef\xbb\xbf"} + "with a mark\r\n",
		// Built with a length: a bare literal holding NUL bytes would end at the first of them.
		std::string{"\xff\xfe\x61\x00\x62\x00", 6},
		std::string{"\xfe\xff\x00\x61\x00\x62", 6},
	};

	for (const std::string& original : contents)
	{
		const std::filesystem::path path = MakeTempFile(original);

		Result<TextDocument> document = Core::TextDocument::Load(path);
		REQUIRE(document);
		REQUIRE(document.GetValue().Save());

		CHECK(ReadAllBytes(path) == original);
	}
}

TEST_CASE("A save clears the modified flag and refreshes the stamp so the save never comes back as somebody else's change")
{
	const std::filesystem::path path = MakeTempFile("original\n");

	Result<TextDocument> loaded = Core::TextDocument::Load(path);
	REQUIRE(loaded);
	Core::TextDocument document = std::move(loaded).GetValue();

	CHECK_FALSE(document.IsModified());
	CHECK_FALSE(document.CheckChangedOnDisk());

	document.GetBuffer().Insert(0, "more ");
	CHECK(document.IsModified());

	REQUIRE(document.Save());
	CHECK_FALSE(document.IsModified());

	CHECK_FALSE(document.CheckChangedOnDisk());
}

TEST_CASE("A save breaks the undo group so typing right after it is a step of its own even where typing would coalesce")
{
	constexpr std::size_t kRightAfterFirstInsert = 1;

	const std::filesystem::path path = MakeTempFile("");

	Result<TextDocument> loaded = Core::TextDocument::Load(path);
	REQUIRE(loaded);
	Core::TextDocument document = std::move(loaded).GetValue();

	Core::TextBuffer& buffer = document.GetBuffer();

	buffer.BeginTransaction();
	buffer.Insert(0, "a");
	buffer.EndTransaction();

	REQUIRE(document.Save());

	buffer.BeginTransaction();
	buffer.Insert(kRightAfterFirstInsert, "b");
	buffer.EndTransaction();

	REQUIRE(buffer.Materialize() == "ab");

	CHECK(buffer.Undo().has_value());
	CHECK(buffer.Materialize() == "a");

	CHECK(buffer.Undo().has_value());
	CHECK(buffer.Materialize().empty());
}

TEST_CASE("A document built by FromContent takes no stamp and so reports no change on disk")
{
	const std::filesystem::path path = MakeTempFile("on disk\n");

	Result<TextDocument> unstampedDocument = Core::TextDocument::FromContent("in memory\n", path);
	REQUIRE(unstampedDocument);

	CHECK_FALSE(unstampedDocument.GetValue().CheckChangedOnDisk());
}

TEST_CASE("A file rewritten by other means is noticed")
{
	const std::filesystem::path path = MakeTempFile("original\n");

	Result<TextDocument> loaded = Core::TextDocument::Load(path);
	REQUIRE(loaded);
	Core::TextDocument document = std::move(loaded).GetValue();

	// A different length as well: two writes in quick succession can share a timestamp, so a test
	// relying on the timestamp alone would eventually flicker.
	WriteFileBytes(path, "replaced from elsewhere\n");

	CHECK(document.CheckChangedOnDisk());
}

TEST_CASE("Reloading is one undo step and undoing it leaves the document out of step with the file and so modified")
{
	const std::filesystem::path path = MakeTempFile("original\n");

	Result<TextDocument> loaded = Core::TextDocument::Load(path);
	REQUIRE(loaded);
	Core::TextDocument document = std::move(loaded).GetValue();

	WriteFileBytes(path, "replaced from elsewhere\n");
	REQUIRE(document.ReloadFromDisk());

	CHECK(document.GetBuffer().Materialize() == "replaced from elsewhere\n");
	CHECK_FALSE(document.IsModified());

	CHECK(document.GetBuffer().Undo().has_value());
	CHECK(document.GetBuffer().Materialize() == "original\n");

	CHECK(document.IsModified());
}

TEST_CASE("A reload that cannot decode leaves the document untouched")
{
	const std::filesystem::path path = MakeTempFile("original\n");

	Result<TextDocument> loaded = Core::TextDocument::Load(path);
	REQUIRE(loaded);
	Core::TextDocument document = std::move(loaded).GetValue();

	const std::string markThenLoneHighSurrogate = std::string{"\xff\xfe\x3d\xd8", 4};
	WriteFileBytes(path, markThenLoneHighSurrogate);

	CHECK_FALSE(document.ReloadFromDisk());
	CHECK(document.GetBuffer().Materialize() == "original\n");
	CHECK_FALSE(document.IsModified());
}

TEST_CASE("AcceptDiskState silences a change without reading it")
{
	const std::filesystem::path path = MakeTempFile("original\n");

	Result<TextDocument> loaded = Core::TextDocument::Load(path);
	REQUIRE(loaded);
	Core::TextDocument document = std::move(loaded).GetValue();

	WriteFileBytes(path, "replaced from elsewhere\n");
	REQUIRE(document.CheckChangedOnDisk());

	document.AcceptDiskState();

	CHECK_FALSE(document.CheckChangedOnDisk());
	CHECK(document.GetBuffer().Materialize() == "original\n");
}

TEST_CASE("NormaliseLineEndings turns CR LF into LF and leaves a lone CR alone")
{
	const NormalisedLineEndings windows = NormaliseLineEndings("a\r\nb");
	CHECK(windows.Content == "a\nb");
	CHECK(windows.Ending == LineEnding::Crlf);
	CHECK_FALSE(windows.Mixed);

	CHECK(NormaliseLineEndings("\r\r\n").Content == "\r\n");
	CHECK(NormaliseLineEndings("a\r").Content == "a\r");
	CHECK(NormaliseLineEndings("a\r").Ending == LineEnding::Lf);

	const NormalisedLineEndings mixed = NormaliseLineEndings("a\r\nb\nc");
	CHECK(mixed.Content == "a\nb\nc");
	CHECK(mixed.Mixed);
	CHECK(mixed.Ending == LineEnding::Lf);

	CHECK(NormaliseLineEndings("").Content.empty());
}
