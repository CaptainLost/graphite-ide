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
