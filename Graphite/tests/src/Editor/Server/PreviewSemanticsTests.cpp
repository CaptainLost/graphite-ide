#include <doctest/doctest.h>

#include "Editor/Server/PreviewSemantics.h"

#include "Core/Text/TextBuffer.h"

#include <filesystem>
#include <string>
#include <vector>

using namespace Graphite;

namespace {

	constexpr std::size_t kTabWidth = 4;
	constexpr std::size_t kWiderTabWidth = 8;
	constexpr bool kTabHoldsDocument = true;
	constexpr bool kPreviewOwnsDocument = false;

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
	semantics.Declare("a.cpp", buffer, kTabWidth, kPreviewOwnsDocument);

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
	semantics.Declare("a.cpp", buffer, kTabWidth, kPreviewOwnsDocument);
	semantics.Declare("a.cpp", buffer, kTabWidth, kPreviewOwnsDocument);

	CHECK(port.CountOf("open") == 1);
	CHECK(port.CountOf("ask") == 1);
}

TEST_CASE("A file a tab already holds is asked about but never opened")
{
	FakePort port;
	PreviewSemantics semantics{port};
	const Core::TextBuffer buffer{"Renderer r;\n"};

	semantics.Tick(1, 0.0);
	semantics.Declare("a.cpp", buffer, kTabWidth, kTabHoldsDocument);

	CHECK(port.CountOf("open") == 0);
	CHECK(port.CountOf("ask") == 1);
}

TEST_CASE("Holds answers for the didOpen this class sent, and for no other")
{
	FakePort port;
	PreviewSemantics semantics{port};
	const Core::TextBuffer buffer{"Renderer r;\n"};

	CHECK_FALSE(semantics.Holds("a.cpp"));

	semantics.Tick(1, 0.0);
	semantics.Declare("a.cpp", buffer, kTabWidth, kPreviewOwnsDocument);

	CHECK(semantics.Holds("a.cpp"));
	CHECK_FALSE(semantics.Holds("b.cpp"));

	semantics.Tick(2, 0.0);
	semantics.Declare("a.cpp", buffer, kTabWidth, kTabHoldsDocument);

	CHECK_FALSE(semantics.Holds("a.cpp"));

	semantics.Tick(3, 0.0);
	semantics.Declare("c.cpp", buffer, kTabWidth, kTabHoldsDocument);

	CHECK_FALSE(semantics.Holds("c.cpp"));

	semantics.Tick(4, 0.0);
	semantics.Declare("d.cpp", buffer, kTabWidth, kPreviewOwnsDocument);

	CHECK(semantics.Holds("d.cpp"));

	semantics.Tick(5, 0.0);
	semantics.Tick(6, 0.0);

	CHECK_FALSE(semantics.Holds("d.cpp"));
}

TEST_CASE("An answer colours the file it was asked about")
{
	FakePort port;
	PreviewSemantics semantics{port};
	const Core::TextBuffer buffer{"Renderer r;\n"};

	semantics.Tick(1, 0.0);
	semantics.Declare("a.cpp", buffer, kTabWidth, kPreviewOwnsDocument);

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
	semantics.Declare("a.cpp", buffer, kTabWidth, kPreviewOwnsDocument);

	semantics.Tick(2, 0.1);
	CHECK(port.CountOf("close") == 0);

	semantics.Tick(3, 0.2);
	CHECK(port.CountOf("close") == 1);
}

TEST_CASE("A declaration inside the grace frame closes nothing and asks nothing again")
{
	FakePort port;
	PreviewSemantics semantics{port};
	const Core::TextBuffer buffer{"Renderer r;\n"};

	semantics.Tick(1, 0.0);
	semantics.Declare("a.cpp", buffer, kTabWidth, kPreviewOwnsDocument);

	semantics.Tick(2, 0.1);
	semantics.Declare("a.cpp", buffer, kTabWidth, kPreviewOwnsDocument);

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
	semantics.Declare("a.cpp", buffer, kTabWidth, kTabHoldsDocument);

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
	semantics.Declare("a.cpp", buffer, kTabWidth, kTabHoldsDocument);
	REQUIRE(semantics.Apply(Tokens(static_cast<LanguageServices::Ticket>(1),
								   {At(0, 0, 8, SemanticKind::Type)})));

	buffer.Insert(0, "const ");

	semantics.Tick(2, 0.1);
	semantics.Declare("a.cpp", buffer, kTabWidth, kTabHoldsDocument);

	CHECK(port.CountOf("ask") == 2);
	CHECK(semantics.SpansOn("a.cpp", 0).empty());
}

TEST_CASE("A tab width change drops the colours and asks again")
{
	FakePort port;
	PreviewSemantics semantics{port};
	const Core::TextBuffer buffer{"\tRenderer r;\n"};

	semantics.Tick(1, 0.0);
	semantics.Declare("a.cpp", buffer, kTabWidth, kPreviewOwnsDocument);
	REQUIRE(semantics.Apply(Tokens(static_cast<LanguageServices::Ticket>(1),
								   {At(0, 1, 8, SemanticKind::Type)})));

	semantics.Tick(2, 0.1);
	semantics.Declare("a.cpp", buffer, kWiderTabWidth, kPreviewOwnsDocument);

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
	semantics.Declare("a.cpp", buffer, kTabWidth, kTabHoldsDocument);

	buffer.Insert(0, "const ");

	semantics.Tick(2, 0.1);
	semantics.Declare("a.cpp", buffer, kTabWidth, kTabHoldsDocument);

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
	semantics.Declare("a.cpp", buffer, kTabWidth, kPreviewOwnsDocument);

	LanguageServices::Answer failed;
	failed.For = static_cast<LanguageServices::Ticket>(1);
	failed.Failed = true;

	REQUIRE(semantics.Apply(failed));

	semantics.Tick(2, 0.5);
	semantics.Declare("a.cpp", buffer, kTabWidth, kPreviewOwnsDocument);
	CHECK(port.CountOf("ask") == 1);

	semantics.Tick(3, 2.5);
	semantics.Declare("a.cpp", buffer, kTabWidth, kPreviewOwnsDocument);
	CHECK(port.CountOf("ask") == 2);
}

TEST_CASE("A tab taking over a previewed file does not close it, and stays quiet after")
{
	FakePort port;
	PreviewSemantics semantics{port};
	const Core::TextBuffer buffer{"Renderer r;\n"};

	semantics.Tick(1, 0.0);
	semantics.Declare("a.cpp", buffer, kTabWidth, kPreviewOwnsDocument);
	semantics.Tick(2, 0.1);
	semantics.Declare("a.cpp", buffer, kTabWidth, kTabHoldsDocument);

	semantics.Tick(3, 0.2);
	semantics.Tick(4, 0.3);

	CHECK(port.CountOf("close") == 0);
}

TEST_CASE("A tab closing over a previewed file reopens it here")
{
	FakePort port;
	PreviewSemantics semantics{port};
	const Core::TextBuffer buffer{"Renderer r;\n"};

	semantics.Tick(1, 0.0);
	semantics.Declare("a.cpp", buffer, kTabWidth, kTabHoldsDocument);
	semantics.Tick(2, 0.1);
	semantics.Declare("a.cpp", buffer, kTabWidth, kPreviewOwnsDocument);

	CHECK(port.CountOf("open") == 1);
	CHECK(port.CountOf("ask") == 2);
}

TEST_CASE("Destruction after a tab takes over a previewed file closes nothing")
{
	FakePort port;

	{
		PreviewSemantics semantics{port};
		const Core::TextBuffer buffer{"Renderer r;\n"};

		semantics.Tick(1, 0.0);
		semantics.Declare("a.cpp", buffer, kTabWidth, kPreviewOwnsDocument);
		semantics.Tick(2, 0.1);
		semantics.Declare("a.cpp", buffer, kTabWidth, kTabHoldsDocument);
	}

	CHECK(port.CountOf("close") == 0);
}

TEST_CASE("Destruction closes what was opened here and nothing else")
{
	FakePort port;

	{
		PreviewSemantics semantics{port};
		const Core::TextBuffer buffer{"Renderer r;\n"};

		semantics.Tick(1, 0.0);
		semantics.Declare("a.cpp", buffer, kTabWidth, kPreviewOwnsDocument);
		semantics.Declare("b.cpp", buffer, kTabWidth, kTabHoldsDocument);
	}

	REQUIRE(port.CountOf("close") == 1);
	CHECK(port.Calls.back().Path == std::filesystem::path{"a.cpp"});
}
