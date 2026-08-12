#include <doctest/doctest.h>

#include "Core/Fs/FileSystem.h"
#include "Core/Text/FileProbe.h"
#include "Core/Text/TextDocument.h"

#include "TempDirectory.h"

#include <atomic>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <ostream>
#include <string>
#include <system_error>
#include <vector>

#ifdef CORE_PLATFORM_WINDOWS
#include <windows.h>

#include <winioctl.h>
#endif

using namespace Core;

namespace {
	TextDocument Make(std::string bytes)
	{
		auto document = TextDocument::FromContent(std::move(bytes), "C:/tmp/sample.txt");
		REQUIRE(document);

		return std::move(document).GetValue();
	}

	void WriteAllBytes(const std::filesystem::path& path, std::string_view contents)
	{
		std::ofstream stream{path, std::ios::binary};
		stream.write(contents.data(), static_cast<std::streamsize>(contents.size()));
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
		static std::atomic<int> counter{0};

		const std::string name = "doc-" + std::to_string(counter.fetch_add(1)) + ".tmp";
		return directory.MakeFile(name, contents);
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

TEST_CASE("A plain LF file loads as it is")
{
	const TextDocument document = Make("one\ntwo\n");

	CHECK(document.GetBuffer().Materialize() == "one\ntwo\n");
	CHECK(document.GetDocumentLineEnding() == LineEnding::Lf);
	CHECK_FALSE(document.WasMixed());
	CHECK(document.GetFileEncoding() == Encoding::Utf8);
	CHECK_FALSE(document.IsReadOnly());
}

TEST_CASE("CRLF is normalised away inside the buffer")
{
	const TextDocument document = Make("one\r\ntwo\r\n");

	// The buffer never holds a '\r', so nothing downstream has to know CRLF exists.
	CHECK(document.GetBuffer().Materialize() == "one\ntwo\n");
	CHECK(document.GetBuffer().GetLineCount() == 3);
	CHECK(document.GetDocumentLineEnding() == LineEnding::Crlf);
}

TEST_CASE("A CRLF file is written back as CRLF")
{
	const TextDocument document = Make("one\r\ntwo\r\n");

	CHECK(document.EncodeToBytes() == "one\r\ntwo\r\n");
}

TEST_CASE("An untouched file comes back byte for byte")
{
	const std::string original = "int main()\r\n{\r\n    return 0;\r\n}\r\n";

	CHECK(Make(original).EncodeToBytes() == original);
}

TEST_CASE("A file with both styles takes the majority and says it was mixed")
{
	const TextDocument document = Make("a\r\nb\r\nc\r\nd\ne\r\n");

	CHECK(document.GetDocumentLineEnding() == LineEnding::Crlf);
	CHECK(document.WasMixed());

	// Saving unifies, which is why the status bar has to show it beforehand.
	CHECK(document.EncodeToBytes() == "a\r\nb\r\nc\r\nd\r\ne\r\n");
}

TEST_CASE("A mostly-LF file with one CRLF takes LF")
{
	const TextDocument document = Make("a\nb\nc\r\nd\n");

	CHECK(document.GetDocumentLineEnding() == LineEnding::Lf);
	CHECK(document.WasMixed());
}

TEST_CASE("A file with no line break at all is LF")
{
	const TextDocument document = Make("no newline here");

	CHECK(document.GetDocumentLineEnding() == LineEnding::Lf);
	CHECK_FALSE(document.WasMixed());
	CHECK(document.EncodeToBytes() == "no newline here");
}

TEST_CASE("An empty file loads as one empty line")
{
	const TextDocument document = Make("");

	CHECK(document.GetBuffer().GetLineCount() == 1);
	CHECK(document.EncodeToBytes() == "");
}

TEST_CASE("A file of nothing but newlines keeps every one of them")
{
	const TextDocument document = Make("\n\n\n");

	CHECK(document.GetBuffer().GetLineCount() == 4);
	CHECK(document.EncodeToBytes() == "\n\n\n");
}

TEST_CASE("A lone carriage return next to a real CRLF round-trips untouched")
{
	// NormaliseLineEndings only ever folds a "\r\n" pair; a '\r' with no '\n' right
	// after it is not one of those and must survive exactly as it was.
	CHECK(Make("a\r\r\nb").EncodeToBytes() == "a\r\r\nb");
}

TEST_CASE("A UTF-8 byte order mark is stripped from the content and put back on save")
{
	const TextDocument document = Make("\xEF\xBB\xBFint main() {}");

	// Left in the content it would be an invisible character in line 1, throwing off
	// every column and showing the file as changed in git the moment it was saved.
	CHECK(document.GetBuffer().Materialize() == "int main() {}");
	CHECK(document.GetFileEncoding() == Encoding::Utf8Bom);
	CHECK(document.EncodeToBytes() == "\xEF\xBB\xBFint main() {}");
}

TEST_CASE("A Visual Studio file with a mark and CRLF round-trips exactly")
{
	const std::string original = "\xEF\xBB\xBF<Project>\r\n</Project>\r\n";

	CHECK(Make(original).EncodeToBytes() == original);
}

TEST_CASE("Content that is not valid UTF-8 loads read-only with its bytes untouched")
{
	// "część" as Windows-1250 writes it.
	const std::string original = "cz\xEA\x9C\xE6\r\nnext line\r\n";
	const TextDocument document = Make(original);

	CHECK(document.IsReadOnly());
	CHECK(document.GetFileEncoding() == Encoding::Unknown);
	CHECK(document.EncodeToBytes() == original);
}

TEST_CASE("The document remembers where it came from")
{
	const TextDocument document = Make("hello");

	CHECK(document.GetPath() == std::filesystem::path{"C:/tmp/sample.txt"});
}

TEST_CASE("A freshly loaded document is not modified")
{
	const TextDocument document = Make("hello");

	CHECK_FALSE(document.IsModified());
}

TEST_CASE("Editing modifies, and undoing all the way back clears it again")
{
	TextDocument document = Make("hello");

	document.GetBuffer().Insert(5, " world");
	CHECK(document.IsModified());

	document.GetBuffer().Undo();
	CHECK_FALSE(document.IsModified());
}

TEST_CASE("Marking saved moves the point that modification is measured from")
{
	TextDocument document = Make("hello");

	document.GetBuffer().Insert(5, "!");
	document.MarkSaved();
	CHECK_FALSE(document.IsModified());

	document.GetBuffer().Undo();
	CHECK(document.IsModified());

	document.GetBuffer().Redo();
	CHECK_FALSE(document.IsModified());
}

TEST_CASE("Edits made after saving are written back with the file's own line ending")
{
	TextDocument document = Make("one\r\ntwo\r\n");

	document.GetBuffer().Insert(document.GetBuffer().GetLength(), "three\n");

	CHECK(document.EncodeToBytes() == "one\r\ntwo\r\nthree\r\n");
}

TEST_CASE("A byte order mark followed by invalid UTF-8 still round-trips through Bytes")
{
	// The mark is real; what follows it is not valid UTF-8 ("część" as Windows-1250
	// writes it). The mark must still come back — losing it would shrink the file by
	// three bytes the moment a read-only document's EncodeToBytes() is written to disk.
	const std::string original = "\xEF\xBB\xBF"
								 "cz\xEA\x9C\xE6\r\nnext line\r\n";
	const TextDocument document = Make(original);

	CHECK(document.IsReadOnly());
	CHECK(document.GetFileEncoding() == Encoding::Unknown);
	CHECK(document.EncodeToBytes() == original);
}

TEST_CASE("Load reads an ordinary file from disk")
{
	TempDirectory temp;
	const std::filesystem::path path = temp.MakeFile("sample.txt", "one\r\ntwo\r\n");

	const auto document = TextDocument::Load(path);
	REQUIRE(document);

	CHECK(document.GetValue().GetBuffer().Materialize() == "one\ntwo\n");
	CHECK(document.GetValue().GetDocumentLineEnding() == LineEnding::Crlf);
}

TEST_CASE("Load succeeds on a legitimately empty file")
{
	// The read loop's own eofbit/failbit on the very first call, with zero bytes ever
	// extracted, must not be mistaken for stream.bad() — the failure this function
	// actually treats as an error.
	TempDirectory temp;
	const std::filesystem::path path = temp.MakeFile("empty.txt", "");

	const auto document = TextDocument::Load(path);
	REQUIRE(document);

	CHECK(document.GetValue().GetBuffer().Materialize() == "");
	CHECK(document.GetValue().GetBuffer().GetLineCount() == 1);
}

TEST_CASE("A file on disk loads with its path and content")
{
	TempDirectory directory;
	const auto path = directory.GetPath() / "main.cpp";
	WriteAllBytes(path, "int main()\r\n{\r\n}\r\n");

	const auto document = TextDocument::Load(path);

	REQUIRE(document);
	CHECK(document.GetValue().GetBuffer().Materialize() == "int main()\n{\n}\n");
	CHECK(document.GetValue().GetDocumentLineEnding() == LineEnding::Crlf);
	CHECK(document.GetValue().GetPath() == path);
}

TEST_CASE("A file read from disk is written back byte for byte")
{
	TempDirectory directory;
	const auto path = directory.GetPath() / "project.xml";
	const std::string original = "\xEF\xBB\xBF<Project>\r\n</Project>\r\n";
	WriteAllBytes(path, original);

	const auto document = TextDocument::Load(path);

	REQUIRE(document);
	CHECK(document.GetValue().EncodeToBytes() == original);
}

TEST_CASE("Loading a file that is not there fails rather than giving an empty document")
{
	TempDirectory directory;

	const auto document = TextDocument::Load(directory.GetPath() / "absent.cpp");

	REQUIRE_FALSE(document);
	CHECK(document.GetError().Code == Core::ErrorCode::NotFound);
}

TEST_CASE("A file whose name is not ASCII loads")
{
	// Paths go through the wide-character API, so a name outside the active codepage
	// has to work — this is what Core::ToUtf8 exists for.
	TempDirectory directory;
	const auto path = directory.GetPath() / Core::FromUtf8("\xC4\x87wiczenie.txt");
	WriteAllBytes(path, "content");

	const auto document = TextDocument::Load(path);

	REQUIRE(document);
	CHECK(document.GetValue().GetBuffer().Materialize() == "content");
	CHECK(document.GetValue().GetPath() == path);
}

TEST_CASE("Loading a directory fails with InvalidArgument rather than NotFound")
{
	TempDirectory directory;

	const auto document = TextDocument::Load(directory.GetPath());

	REQUIRE_FALSE(document);
	CHECK(document.GetError().Code == Core::ErrorCode::InvalidArgument);
}

#ifdef CORE_PLATFORM_WINDOWS
TEST_CASE("Load refuses a file larger than the maximum size, before reading it")
{
	TempDirectory directory;
	const auto path = directory.GetPath() / "huge.bin";
	WriteAllBytes(path, "");

	// Marking the file sparse first means growing it reserves no real disk clusters,
	// regardless of how much free space the volume actually has.
	if (!MarkSparse(path))
	{
		return;
	}

	// Growing the file this way is a metadata-only operation on NTFS — no bytes are
	// actually written — so this exercises the real size check in Load against a real
	// file without the test paying for a genuine half-gigabyte write.
	std::error_code resizeError;
	std::filesystem::resize_file(path, MaximumFileSize + 1, resizeError);
	REQUIRE_FALSE(resizeError);

	const auto document = TextDocument::Load(path);

	REQUIRE_FALSE(document);
	CHECK(document.GetError().Code == Core::ErrorCode::InvalidArgument);
	CHECK(document.GetError().Message.find(std::to_string(MaximumFileSize + 1)) != std::string::npos);
}
#else
TEST_CASE("Load refuses a file larger than the maximum size, before reading it" * doctest::skip())
{
}
#endif

#ifdef CORE_PLATFORM_WINDOWS
TEST_CASE("Load reports AccessDenied rather than NotFound for a file it cannot open")
{
	// A sharing violation from a handle held open in-process, deterministic and
	// requiring no second process: opening with dwShareMode 0 denies every other
	// handle, including the ifstream Load opens next.
	TempDirectory directory;
	const auto path = directory.GetPath() / "locked.txt";
	WriteAllBytes(path, "content");

	const HANDLE handle = ::CreateFileW(path.c_str(), GENERIC_READ, /*dwShareMode=*/0, nullptr,
										OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
	REQUIRE(handle != INVALID_HANDLE_VALUE);

	const auto document = TextDocument::Load(path);

	// Closed before any assertion can fail the test, so a locked file never survives
	// into TempDirectory's destructor and takes a later test down with it.
	::CloseHandle(handle);

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
	const auto document = TextDocument::FromContent(
		std::string("\xFF\xFEH\0i\0", 6), "C:/tmp/utf16.txt");

	REQUIRE(document);
	CHECK(document.GetValue().GetBuffer().Materialize() == "Hi");
	CHECK(document.GetValue().GetFileEncoding() == Encoding::Utf16Le);
	CHECK_FALSE(document.GetValue().IsReadOnly());
}

TEST_CASE("A UTF-16 file without a mark opens too")
{
	const auto document = TextDocument::FromContent(
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
		const auto document = TextDocument::FromContent(original, "C:/tmp/round.txt");

		REQUIRE(document);
		CHECK(document.GetValue().EncodeToBytes() == original);
	}
}

TEST_CASE("CRLF in a UTF-16 file is normalised and restored like any other file")
{
	const std::string original = std::string("a\0\r\0\n\0b\0", 8);
	const auto document = TextDocument::FromContent(original, "C:/tmp/crlf.txt");

	REQUIRE(document);
	CHECK(document.GetValue().GetBuffer().Materialize() == "a\nb");
	CHECK(document.GetValue().GetDocumentLineEnding() == LineEnding::Crlf);
	CHECK(document.GetValue().EncodeToBytes() == original);
}

TEST_CASE("A damaged UTF-16 file is refused with a message naming the damage")
{
	// A low surrogate with nothing before it, behind a mark so the encoding is certain.
	const auto document = TextDocument::FromContent(
		std::string("\xFF\xFE\x42\xDE", 4), "C:/tmp/damaged.txt");

	REQUIRE_FALSE(document);
	CHECK(document.GetError().Message.find("damaged") != std::string::npos);
	CHECK(document.GetError().Message.find("surrogate") != std::string::npos);
}

TEST_CASE("An odd byte count is refused as damage rather than opened short")
{
	const auto document = TextDocument::FromContent(
		std::string("\xFF\xFEH\0i", 5), "C:/tmp/odd.txt");

	REQUIRE_FALSE(document);
	CHECK(document.GetError().Message.find("odd") != std::string::npos);
}

TEST_CASE("The probe and the document agree about a file that changes shape past the sample")
{
	// Unmarked UTF-16 LE, clean for exactly the 8 KB the probe reads, with a form feed
	// just past the end of it. Nothing smaller than ProbeSampleSize can catch this: the
	// probe and the document only ever disagree when the document can see bytes the
	// probe could not.
	std::string bytes;
	bytes.reserve(ProbeSampleSize + 8);

	while (bytes.size() < ProbeSampleSize)
		bytes += std::string("a\0", 2);

	REQUIRE(bytes.size() == ProbeSampleSize);

	bytes += std::string("\x0C\0", 2);
	bytes += std::string("b\0", 2);

	TempDirectory directory;
	const auto path = directory.GetPath() / "tail.txt";
	WriteAllBytes(path, bytes);

	const auto probe = ProbeFile(path);

	REQUIRE(probe);
	CHECK(probe.GetValue().FileEncoding == Encoding::Utf16Le);
	CHECK(probe.GetValue().Kind == FileKind::Text);

	// The document now reads the same prefix and reaches the same verdict, so the file it
	// loads is the file the probe cleared. The strict rule then judges the whole decoded
	// content and refuses it, rather than opening raw UTF-16 as a writable UTF-8 buffer
	// whose first edit would be written into the middle of a UTF-16 file.
	const auto document = TextDocument::Load(path);

	REQUIRE_FALSE(document);
	CHECK(document.GetError().Message.find("UTF-16 LE") != std::string::npos);
}

TEST_CASE("A marked UTF-16 file keeps its control characters instead of being refused")
{
	// "a", a form feed — the page break Emacs writes into C source — "b", and U+0085, a
	// C1 control. The identical text saved as UTF-8 opens without complaint, because that
	// path validates rather than shape-checks, so refusing it here would make the encoding
	// decide whether a file may be edited. The mark settles what this is, and Decode and
	// Encode are exact inverses, so both controls go back to disk as the bytes they came
	// from. The literal is split because 'a' is a hex digit and would otherwise be eaten
	// by the \xFE escape before it.
	const std::string original = std::string("\xFF\xFE"
											 "a\0\x0C\0b\0\x85\0",
											 10);

	const auto document = TextDocument::FromContent(original, "C:/tmp/pagebreak.c");

	REQUIRE(document);
	CHECK(document.GetValue().GetFileEncoding() == Encoding::Utf16Le);
	CHECK_FALSE(document.GetValue().IsReadOnly());
	CHECK(document.GetValue().GetBuffer().Materialize() == std::string("a\x0C"
																	   "b\xC2\x85",
																	   5));
	CHECK(document.GetValue().EncodeToBytes() == original);
}

TEST_CASE("A UTF-32 file is refused rather than opened as UTF-16")
{
	// What Set-Content -Encoding UTF32 writes for "Hi". The UTF-32 LE mark FF FE 00 00
	// opens with the UTF-16 LE mark, so detection names UTF-16 and a mark overrules every
	// other check — the codec is the only gate left, and U+0000 is a codepoint it decodes
	// without complaint. Refused now, and the reason is that the decoded content is full
	// of NUL bytes, which no text is.
	const auto document =
		TextDocument::FromContent(std::string("\xFF\xFE\0\0H\0\0\0i\0\0\0", 12),
								  "C:/tmp/utf32.txt");

	REQUIRE_FALSE(document);
	CHECK(document.GetError().Message.find("UTF-16 LE") != std::string::npos);
}

TEST_CASE("Saving round trips every encoding byte for byte")
{
	const std::vector<std::string> contents = {
		"line one\r\nline two\r\n",
		std::string{"\xef\xbb\xbf"} + "with a mark\r\n",
		// The std::string{pointer, length} constructor, not a bare literal: a literal
		// holding "\xff\xfe" followed by NUL bytes truncates at the first NUL, which is
		// the defect that cost this project a test during stage 2.5.
		std::string{"\xff\xfe\x61\x00\x62\x00", 6},
		std::string{"\xfe\xff\x00\x61\x00\x62", 6},
	};

	for (const std::string& original : contents)
	{
		const std::filesystem::path path = MakeTempFile(original);

		auto document = Core::TextDocument::Load(path);
		REQUIRE(document);
		REQUIRE(document.GetValue().Save());

		CHECK(ReadAllBytes(path) == original);
	}
}

TEST_CASE("A save clears the modified flag and refreshes the stamp")
{
	const std::filesystem::path path = MakeTempFile("original\n");

	auto loaded = Core::TextDocument::Load(path);
	REQUIRE(loaded);
	Core::TextDocument document = std::move(loaded).GetValue();

	CHECK_FALSE(document.IsModified());
	CHECK_FALSE(document.CheckChangedOnDisk());

	document.GetBuffer().Insert(0, "more ");
	CHECK(document.IsModified());

	REQUIRE(document.Save());
	CHECK_FALSE(document.IsModified());

	// The stamp now describes the file we just wrote, which is what stops our own save
	// from arriving later as somebody else's change.
	CHECK_FALSE(document.CheckChangedOnDisk());
}

TEST_CASE("A save breaks the undo group, so typing after it is a step of its own")
{
	const std::filesystem::path path = MakeTempFile("");

	auto loaded = Core::TextDocument::Load(path);
	REQUIRE(loaded);
	Core::TextDocument document = std::move(loaded).GetValue();

	Core::TextBuffer& buffer = document.GetBuffer();

	buffer.BeginTransaction();
	buffer.Insert(0, "a");
	buffer.EndTransaction();

	REQUIRE(document.Save());

	// Consecutive typing at the caret's own advance coalesces, so without the
	// BreakUndoGroup in Save this second insertion joins the first and one Ctrl+Z takes
	// both — undoing past a save the user watched succeed. The design requires
	// type-save-type to be two undo steps, and nothing asserted it until now.
	buffer.BeginTransaction();
	buffer.Insert(1, "b");
	buffer.EndTransaction();

	REQUIRE(buffer.Materialize() == "ab");

	CHECK(buffer.Undo());
	CHECK(buffer.Materialize() == "a");

	CHECK(buffer.Undo());
	CHECK(buffer.Materialize().empty());
}

TEST_CASE("A document that never took a stamp reports no change on disk")
{
	// FromContent takes no stamp, which is the same state Load leaves behind when
	// last_write_time fails — the case a test cannot provoke, and the one this guards.
	// A default-constructed stamp is not "no stamp": it compares unequal to every real
	// file, so this used to answer "changed" for good. A clean document would then reload
	// on every watcher event and every focus regain, each reload stacking another
	// transaction onto the undo stack, and a dirty one would re-queue its conflict
	// question on every sweep.
	const std::filesystem::path path = MakeTempFile("on disk\n");

	auto document = Core::TextDocument::FromContent("in memory\n", path);
	REQUIRE(document);

	CHECK_FALSE(document.GetValue().CheckChangedOnDisk());
}

TEST_CASE("A file rewritten by other means is noticed")
{
	const std::filesystem::path path = MakeTempFile("original\n");

	auto loaded = Core::TextDocument::Load(path);
	REQUIRE(loaded);
	Core::TextDocument document = std::move(loaded).GetValue();

	// A different length as well as different content. The check compares size and
	// modification time, and two writes in quick succession can share a timestamp — a
	// test relying on the timestamp alone is a test that eventually flickers.
	WriteAllBytes(path, "replaced from elsewhere\n");

	CHECK(document.CheckChangedOnDisk());
}

TEST_CASE("Reloading is one undo step")
{
	const std::filesystem::path path = MakeTempFile("original\n");

	auto loaded = Core::TextDocument::Load(path);
	REQUIRE(loaded);
	Core::TextDocument document = std::move(loaded).GetValue();

	WriteAllBytes(path, "replaced from elsewhere\n");
	REQUIRE(document.ReloadFromDisk());

	CHECK(document.GetBuffer().Materialize() == "replaced from elsewhere\n");
	CHECK_FALSE(document.IsModified());

	CHECK(document.GetBuffer().Undo());
	CHECK(document.GetBuffer().Materialize() == "original\n");

	// Undoing a reload puts the document out of step with the file again, which is
	// exactly what modified means.
	CHECK(document.IsModified());
}

TEST_CASE("A reload that cannot decode leaves the document untouched")
{
	const std::filesystem::path path = MakeTempFile("original\n");

	auto loaded = Core::TextDocument::Load(path);
	REQUIRE(loaded);
	Core::TextDocument document = std::move(loaded).GetValue();

	// A lone high surrogate behind a UTF-16 LE mark: recognised as UTF-16, impossible to
	// decode.
	WriteAllBytes(path, std::string{"\xff\xfe\x3d\xd8", 4});

	CHECK_FALSE(document.ReloadFromDisk());
	CHECK(document.GetBuffer().Materialize() == "original\n");
	CHECK_FALSE(document.IsModified());
}

TEST_CASE("AcceptDiskState silences a change without reading it")
{
	const std::filesystem::path path = MakeTempFile("original\n");

	auto loaded = Core::TextDocument::Load(path);
	REQUIRE(loaded);
	Core::TextDocument document = std::move(loaded).GetValue();

	WriteAllBytes(path, "replaced from elsewhere\n");
	REQUIRE(document.CheckChangedOnDisk());

	document.AcceptDiskState();

	CHECK_FALSE(document.CheckChangedOnDisk());
	CHECK(document.GetBuffer().Materialize() == "original\n");
}
