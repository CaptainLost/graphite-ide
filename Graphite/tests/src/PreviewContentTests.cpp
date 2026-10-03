#include <doctest/doctest.h>

#include "Search/PreviewContent.h"

#include "Core/Text/DocumentSet.h"
#include "Editor/Server/PreviewSemantics.h"
#include "Editor/Overlays/SemanticOverlay.h"
#include "Language/Syntax/Token.h"

#include "TempDirectory.h"

#include <fstream>
#include <string>
#include <vector>

using namespace Graphite;

TEST_CASE("A file that is not open is read from disk")
{
	TempDirectory temp;
	const std::filesystem::path file = temp.MakeFile("a.cpp", "int x;\nint y;\n");

	Core::DocumentSet documents;
	PreviewContent content;

	content.Show(file, documents);

	REQUIRE(content.GetState() == PreviewState::Ready);
	REQUIRE(content.GetBuffer() != nullptr);
	CHECK(content.GetBuffer()->Materialize() == "int x;\nint y;\n");
}

TEST_CASE("An open document's buffer wins over the file on disk")
{
	TempDirectory temp;
	const std::filesystem::path file = temp.MakeFile("a.cpp", "on disk\n");

	Core::DocumentSet documents;

	REQUIRE(documents.Open(file, true) == Core::OpenOutcome::Opened);

	const auto index = documents.Find(file);
	REQUIRE(index.has_value());

	Core::OpenItem& item = documents.At(*index);
	REQUIRE(item.Document.has_value());

	// The unsaved edit the preview has to show. Insert rather than replace, so the test is
	// about which source is read and not about how a buffer is rebuilt.
	item.Document->GetBuffer().Insert(0, "edited ");

	PreviewContent content;
	content.Show(file, documents);

	REQUIRE(content.GetState() == PreviewState::Ready);
	REQUIRE(content.GetBuffer() != nullptr);
	CHECK(content.GetBuffer()->Materialize() == "edited on disk\n");
	CHECK(content.GetBuffer() == &item.Document->GetBuffer());
}

TEST_CASE("A file that has gone reports itself unreadable rather than empty")
{
	TempDirectory temp;
	const std::filesystem::path file = temp.GetPath() / "never-existed.cpp";

	Core::DocumentSet documents;
	PreviewContent content;

	content.Show(file, documents);

	CHECK(content.GetState() == PreviewState::Unreadable);
	CHECK_FALSE(content.GetDetail().empty());
	CHECK(content.GetBuffer() == nullptr);
}

TEST_CASE("A binary file is skipped rather than shown")
{
	TempDirectory temp;
	const std::filesystem::path file = temp.GetPath() / "blob.bin";

	{
		std::ofstream stream{file, std::ios::binary | std::ios::trunc};
		const std::string bytes("\0\1\2\0\3binary", 11);
		stream.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
	}

	Core::DocumentSet documents;
	PreviewContent content;

	content.Show(file, documents);

	CHECK(content.GetState() == PreviewState::Skipped);
	CHECK_FALSE(content.GetDetail().empty());
	CHECK(content.GetBuffer() == nullptr);
}

TEST_CASE("Showing a second file replaces the first")
{
	TempDirectory temp;
	const std::filesystem::path first = temp.MakeFile("first.cpp", "first\n");
	const std::filesystem::path second = temp.MakeFile("second.cpp", "second\n");

	Core::DocumentSet documents;
	PreviewContent content;

	content.Show(first, documents);
	content.Show(second, documents);

	REQUIRE(content.GetState() == PreviewState::Ready);
	CHECK(content.GetPath() == second);
	CHECK(content.GetBuffer()->Materialize() == "second\n");
}

TEST_CASE("A file closed while its preview is showing falls back to disk")
{
	TempDirectory temp;
	const std::filesystem::path file = temp.MakeFile("a.cpp", "on disk\n");

	Core::DocumentSet documents;

	REQUIRE(documents.Open(file, true) == Core::OpenOutcome::Opened);

	const auto index = documents.Find(file);
	REQUIRE(index.has_value());

	PreviewContent content;
	content.Show(file, documents);

	REQUIRE(content.GetState() == PreviewState::Ready);

	documents.Close(*index);
	content.Show(file, documents);

	REQUIRE(content.GetState() == PreviewState::Ready);
	CHECK(content.GetBuffer()->Materialize() == "on disk\n");
}

TEST_CASE("A previewed file is coloured by the lexer for its extension")
{
	TempDirectory temp;
	const std::filesystem::path file = temp.MakeFile("a.cpp", "int x;\n");

	Core::DocumentSet documents;
	PreviewContent content;

	content.Show(file, documents);

	const std::vector<Token>& tokens = content.TokensOf(0);

	REQUIRE_FALSE(tokens.empty());
	CHECK(tokens.front().Kind == TokenKind::Keyword);
}

TEST_CASE("A file with no lexer is left uncoloured rather than coloured wrongly")
{
	TempDirectory temp;
	const std::filesystem::path file = temp.MakeFile("notes.unknownext", "int x;\n");

	Core::DocumentSet documents;
	PreviewContent content;

	content.Show(file, documents);

	REQUIRE(content.GetState() == PreviewState::Ready);
	CHECK(content.TokensOf(0).empty());
}

TEST_CASE("Changing the previewed path resets the colours with it")
{
	TempDirectory temp;

	// A file whose first line opens a block comment, so a cache carried across would colour
	// the next file's first line as comment text.
	const std::filesystem::path commented = temp.MakeFile("first.cpp", "/* open\nstill\n");
	const std::filesystem::path plain = temp.MakeFile("second.cpp", "int x;\n");

	Core::DocumentSet documents;
	PreviewContent content;

	content.Show(commented, documents);

	REQUIRE_FALSE(content.TokensOf(0).empty());
	REQUIRE(content.TokensOf(0).front().Kind == TokenKind::Comment);

	// Line 1 of the first file is inside the comment; asking for it is what fills the cache's
	// entry states past line 0, which is exactly what must not survive the switch.
	REQUIRE_FALSE(content.TokensOf(1).empty());

	content.Show(plain, documents);

	const std::vector<Token>& tokens = content.TokensOf(0);

	REQUIRE_FALSE(tokens.empty());
	CHECK(tokens.front().Kind == TokenKind::Keyword);
}

TEST_CASE("Colours follow an edit to the open document being previewed")
{
	TempDirectory temp;
	const std::filesystem::path file = temp.MakeFile("a.cpp", "int x;\n");

	Core::DocumentSet documents;

	REQUIRE(documents.Open(file, true) == Core::OpenOutcome::Opened);

	const auto index = documents.Find(file);
	REQUIRE(index.has_value());

	Core::OpenItem& item = documents.At(*index);
	REQUIRE(item.Document.has_value());

	PreviewContent content;
	content.Show(file, documents);

	REQUIRE(content.TokensOf(0).front().Kind == TokenKind::Keyword);

	// Turns the line into a comment. SyntaxCache notices the buffer's revision moved without
	// an Invalidate and re-lexes, which is the behaviour this pins.
	item.Document->GetBuffer().Insert(0, "// ");
	content.Show(file, documents);

	CHECK(content.TokensOf(0).front().Kind == TokenKind::Comment);
}

TEST_CASE("Clear forgets the file")
{
	TempDirectory temp;
	const std::filesystem::path file = temp.MakeFile("a.cpp", "text\n");

	Core::DocumentSet documents;
	PreviewContent content;

	content.Show(file, documents);
	content.Clear();

	CHECK(content.GetState() == PreviewState::Empty);
	CHECK(content.GetBuffer() == nullptr);
	CHECK(content.GetPath().empty());
}

namespace {

	// The same recording port PreviewSemanticsTests uses, kept separate rather than shared:
	// a header holding one fake for two suites is a header that grows a reason to change for
	// each of them.
	class RecordingPort final : public PreviewServerPort
	{
	public:
		void Open(const std::filesystem::path& path, LanguageId, std::string) override
		{
			Opened.push_back(path);
		}

		LanguageServices::Ticket Ask(const std::filesystem::path& path, LanguageId) override
		{
			Asked.push_back(path);

			return static_cast<LanguageServices::Ticket>(++Tickets);
		}

		void Close(const std::filesystem::path& path, LanguageId) override
		{
			Closed.push_back(path);
		}

		std::vector<std::filesystem::path> Opened;
		std::vector<std::filesystem::path> Asked;
		std::vector<std::filesystem::path> Closed;
		std::uint64_t Tickets = 0;
	};
}

TEST_CASE("A preview with no mediator has no spans")
{
	TempDirectory temp;
	const std::filesystem::path file = temp.MakeFile("a.cpp", "int x;\n");

	Core::DocumentSet documents;
	PreviewContent content;

	content.Show(file, documents);

	CHECK(content.SpansOn(0).empty());
}

TEST_CASE("Showing a file declares it to the mediator")
{
	TempDirectory temp;
	const std::filesystem::path file = temp.MakeFile("a.cpp", "Renderer r;\n");

	RecordingPort port;
	PreviewSemantics semantics{port};
	Core::DocumentSet documents;
	PreviewContent content{&semantics};

	semantics.Tick(1, 0.0);
	content.Show(file, documents);

	REQUIRE(port.Asked.size() == 1);
	CHECK(port.Asked[0] == file);

	// Not open in any tab, so this preview is the one telling the server about it.
	REQUIRE(port.Opened.size() == 1);
	CHECK(port.Opened[0] == file);
}

TEST_CASE("A file with a tab open is declared as one the editor holds")
{
	TempDirectory temp;
	const std::filesystem::path file = temp.MakeFile("a.cpp", "Renderer r;\n");

	RecordingPort port;
	PreviewSemantics semantics{port};
	Core::DocumentSet documents;

	REQUIRE(documents.Open(file, true) == Core::OpenOutcome::Opened);

	PreviewContent content{&semantics};

	semantics.Tick(1, 0.0);
	content.Show(file, documents);

	CHECK(port.Opened.empty());
	CHECK(port.Asked.size() == 1);
}

TEST_CASE("The spans of the file being shown are the ones handed back")
{
	TempDirectory temp;
	const std::filesystem::path file = temp.MakeFile("a.cpp", "Renderer r;\n");

	RecordingPort port;
	PreviewSemantics semantics{port};
	Core::DocumentSet documents;
	PreviewContent content{&semantics};

	semantics.Tick(1, 0.0);
	content.Show(file, documents);

	LanguageServices::Answer answer;
	answer.For = static_cast<LanguageServices::Ticket>(1);
	answer.SemanticTokens = {SemanticToken{0, Utf16Column(0), 8, SemanticKind::Type}};

	REQUIRE(semantics.Apply(answer));

	const std::vector<SemanticSpan>& spans = content.SpansOn(0);

	REQUIRE(spans.size() == 1);
	CHECK(spans[0].Kind == TokenKind::SemanticType);
}

TEST_CASE("A preview asks to be centred once per hit line, so the reader can scroll it after")
{
	TempDirectory temp;
	const std::filesystem::path file = temp.MakeFile("a.cpp", "a\nb\nc\n");

	Core::DocumentSet documents;
	PreviewContent content;

	content.Show(file, documents);

	CHECK(content.TakeCentring(1));
	CHECK_FALSE(content.TakeCentring(1));
	CHECK(content.TakeCentring(2));
}

TEST_CASE("Showing another file asks to be centred again, even on the same line")
{
	TempDirectory temp;
	const std::filesystem::path first = temp.MakeFile("first.cpp", "a\nb\n");
	const std::filesystem::path second = temp.MakeFile("second.cpp", "a\nb\n");

	Core::DocumentSet documents;
	PreviewContent content;

	content.Show(first, documents);
	REQUIRE(content.TakeCentring(1));

	content.Show(second, documents);
	CHECK(content.TakeCentring(1));

	content.Clear();
	content.Show(second, documents);
	CHECK(content.TakeCentring(1));
}
