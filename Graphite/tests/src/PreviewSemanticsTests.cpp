#include <doctest/doctest.h>

#include "Editor/PreviewSemantics.h"

#include "Core/Text/TextBuffer.h"

#include <filesystem>
#include <string>
#include <vector>

using namespace Graphite;

namespace {

	// Records what it was told instead of talking to a process. Every claim this feature makes
	// about a server — one didOpen for two panes, none for a file with a tab, one didClose and
	// not before its time — is a claim about this log.
	class FakePort final : public PreviewServerPort
	{
	public:
		struct Call
		{
			std::string What;
			std::filesystem::path Path;
			std::string Text;
		};

		void Open(const std::filesystem::path& path, LanguageId, std::string text) override
		{
			Calls.push_back({"open", path, std::move(text)});
		}

		LanguageServices::Ticket Ask(const std::filesystem::path& path, LanguageId) override
		{
			Calls.push_back({"ask", path, {}});

			return static_cast<LanguageServices::Ticket>(++Tickets);
		}

		void Close(const std::filesystem::path& path, LanguageId) override
		{
			Calls.push_back({"close", path, {}});
		}

		std::size_t CountOf(const std::string& what) const
		{
			std::size_t count = 0;

			for (const Call& call : Calls)
			{
				if (call.What == what)
				{
					++count;
				}
			}

			return count;
		}

		std::vector<Call> Calls;
		std::uint64_t Tickets = 0;
	};

	LanguageServices::Answer Tokens(LanguageServices::Ticket ticket,
									std::vector<SemanticToken> tokens)
	{
		LanguageServices::Answer answer;
		answer.For = ticket;
		answer.SemanticTokens = std::move(tokens);

		return answer;
	}

	SemanticToken At(std::size_t line, std::size_t column, std::size_t length, SemanticKind kind)
	{
		return SemanticToken{line, Utf16Column(column), length, kind};
	}
}

TEST_CASE("Declaring a file opens it with the server and asks for its tokens")
{
	FakePort port;
	PreviewSemantics semantics{port};
	const Core::TextBuffer buffer{"Renderer r;\n"};

	semantics.Tick(1, 0.0);
	semantics.Declare("a.cpp", buffer, 4, false);

	REQUIRE(port.Calls.size() == 2);
	CHECK(port.Calls[0].What == "open");
	CHECK(port.Calls[0].Text == "Renderer r;\n");
	CHECK(port.Calls[1].What == "ask");
}

TEST_CASE("Two panes showing one file share a single open and a single request")
{
	FakePort port;
	PreviewSemantics semantics{port};
	const Core::TextBuffer buffer{"Renderer r;\n"};

	semantics.Tick(1, 0.0);
	semantics.Declare("a.cpp", buffer, 4, false);
	semantics.Declare("a.cpp", buffer, 4, false);

	CHECK(port.CountOf("open") == 1);
	CHECK(port.CountOf("ask") == 1);
}

TEST_CASE("A file a tab already holds is asked about but never opened")
{
	// The editor sent the didOpen and is still drawing the document. A second one here would be
	// a duplicate, and the didClose that eventually follows it would pull the file out from
	// under DocumentWindows.
	FakePort port;
	PreviewSemantics semantics{port};
	const Core::TextBuffer buffer{"Renderer r;\n"};

	semantics.Tick(1, 0.0);
	semantics.Declare("a.cpp", buffer, 4, true);

	CHECK(port.CountOf("open") == 0);
	CHECK(port.CountOf("ask") == 1);
}

TEST_CASE("An answer colours the file it was asked about")
{
	FakePort port;
	PreviewSemantics semantics{port};
	const Core::TextBuffer buffer{"Renderer r;\n"};

	semantics.Tick(1, 0.0);
	semantics.Declare("a.cpp", buffer, 4, false);

	CHECK(semantics.Apply(Tokens(static_cast<LanguageServices::Ticket>(1),
								 {At(0, 0, 8, SemanticKind::Type)})));

	const std::vector<SemanticSpan>& spans = semantics.SpansOn("a.cpp", 0);

	REQUIRE(spans.size() == 1);
	CHECK(spans[0].From == 0);
	CHECK(spans[0].To == 8);
	CHECK(spans[0].Kind == TokenKind::SemanticType);
}

TEST_CASE("An answer nothing here asked for is left for somebody else")
{
	FakePort port;
	PreviewSemantics semantics{port};

	CHECK_FALSE(semantics.Apply(Tokens(static_cast<LanguageServices::Ticket>(7), {})));
}

TEST_CASE("Spans are empty for a file that was never declared")
{
	FakePort port;
	PreviewSemantics semantics{port};

	CHECK(semantics.SpansOn("nowhere.cpp", 0).empty());
}

TEST_CASE("A file nobody declares is closed, but not in the frame the declarations stop")
{
	FakePort port;
	PreviewSemantics semantics{port};
	const Core::TextBuffer buffer{"Renderer r;\n"};

	semantics.Tick(1, 0.0);
	semantics.Declare("a.cpp", buffer, 4, false);

	// The frame after the last declaration: the grace frame, and nothing closes in it.
	semantics.Tick(2, 0.1);
	CHECK(port.CountOf("close") == 0);

	semantics.Tick(3, 0.2);
	CHECK(port.CountOf("close") == 1);
}

TEST_CASE("A declaration inside the grace frame closes nothing and asks nothing again")
{
	// A pane's selection can drop for one frame while its groups are rebuilt. Paying a full
	// re-parse for that blink is exactly what the grace frame exists to prevent.
	FakePort port;
	PreviewSemantics semantics{port};
	const Core::TextBuffer buffer{"Renderer r;\n"};

	semantics.Tick(1, 0.0);
	semantics.Declare("a.cpp", buffer, 4, false);

	// The redeclare lands inside the grace frame itself (between Tick(2) and Tick(3)) — that is
	// the blink this case is about. A redeclare arriving only once frame 3's Tick has already
	// run the close check would be a frame too late to save anything: closing is decided
	// synchronously inside that Tick, from the call history alone, the same way the previous
	// case proves it.
	semantics.Tick(2, 0.1);
	semantics.Declare("a.cpp", buffer, 4, false);

	semantics.Tick(3, 0.2);

	CHECK(port.CountOf("close") == 0);
	CHECK(port.CountOf("open") == 1);
	CHECK(port.CountOf("ask") == 1);
}

TEST_CASE("A file a tab holds is never closed from here")
{
	FakePort port;
	PreviewSemantics semantics{port};
	const Core::TextBuffer buffer{"Renderer r;\n"};

	semantics.Tick(1, 0.0);
	semantics.Declare("a.cpp", buffer, 4, true);

	semantics.Tick(2, 0.1);
	semantics.Tick(3, 0.2);

	CHECK(port.CountOf("close") == 0);
}

TEST_CASE("An edit to a document with a tab drops its colours and asks again")
{
	FakePort port;
	PreviewSemantics semantics{port};
	Core::TextBuffer buffer{"Renderer r;\n"};

	semantics.Tick(1, 0.0);
	semantics.Declare("a.cpp", buffer, 4, true);
	REQUIRE(semantics.Apply(Tokens(static_cast<LanguageServices::Ticket>(1),
								   {At(0, 0, 8, SemanticKind::Type)})));

	buffer.Insert(0, "const ");

	semantics.Tick(2, 0.1);
	semantics.Declare("a.cpp", buffer, 4, true);

	CHECK(port.CountOf("ask") == 2);
	CHECK(semantics.SpansOn("a.cpp", 0).empty());
}

TEST_CASE("A tab width change drops the colours and asks again")
{
	// The overlay's offsets are measured in tab-expanded text, so every span on a line holding a
	// tab moves when the width does.
	FakePort port;
	PreviewSemantics semantics{port};
	const Core::TextBuffer buffer{"\tRenderer r;\n"};

	semantics.Tick(1, 0.0);
	semantics.Declare("a.cpp", buffer, 4, false);
	REQUIRE(semantics.Apply(Tokens(static_cast<LanguageServices::Ticket>(1),
								   {At(0, 1, 8, SemanticKind::Type)})));

	semantics.Tick(2, 0.1);
	semantics.Declare("a.cpp", buffer, 8, false);

	CHECK(port.CountOf("ask") == 2);
	CHECK(port.CountOf("open") == 1);
	CHECK(semantics.SpansOn("a.cpp", 0).empty());
}

TEST_CASE("An answer whose ticket has been superseded is dropped")
{
	FakePort port;
	PreviewSemantics semantics{port};
	Core::TextBuffer buffer{"Renderer r;\n"};

	semantics.Tick(1, 0.0);
	semantics.Declare("a.cpp", buffer, 4, true);

	buffer.Insert(0, "const ");

	semantics.Tick(2, 0.1);
	semantics.Declare("a.cpp", buffer, 4, true);

	// The first request's answer, arriving after the edit made it obsolete. Applying it would
	// colour words that have moved six characters to the right.
	CHECK_FALSE(semantics.Apply(Tokens(static_cast<LanguageServices::Ticket>(1),
									   {At(0, 0, 8, SemanticKind::Type)})));
	CHECK(semantics.SpansOn("a.cpp", 0).empty());
}

TEST_CASE("A failed request is tried again, but only after its retry clock")
{
	FakePort port;
	PreviewSemantics semantics{port};
	const Core::TextBuffer buffer{"Renderer r;\n"};

	semantics.Tick(1, 0.0);
	semantics.Declare("a.cpp", buffer, 4, false);

	LanguageServices::Answer failed;
	failed.For = static_cast<LanguageServices::Ticket>(1);
	failed.Failed = true;

	REQUIRE(semantics.Apply(failed));

	semantics.Tick(2, 0.5);
	semantics.Declare("a.cpp", buffer, 4, false);
	CHECK(port.CountOf("ask") == 1);

	semantics.Tick(3, 2.5);
	semantics.Declare("a.cpp", buffer, 4, false);
	CHECK(port.CountOf("ask") == 2);
}

TEST_CASE("Destruction closes what was opened here and nothing else")
{
	FakePort port;

	{
		PreviewSemantics semantics{port};
		const Core::TextBuffer buffer{"Renderer r;\n"};

		semantics.Tick(1, 0.0);
		semantics.Declare("a.cpp", buffer, 4, false);
		semantics.Declare("b.cpp", buffer, 4, true);
	}

	REQUIRE(port.CountOf("close") == 1);
	CHECK(port.Calls.back().Path == std::filesystem::path{"a.cpp"});
}
