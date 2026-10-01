#include <doctest/doctest.h>

#include "Editor/LspAnswerRouter.h"

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <string>
#include <utility>
#include <vector>

using namespace Graphite;

namespace {

	using Ticket = LanguageServices::Ticket;
	using Answer = LanguageServices::Answer;

	// Records what it was asked and answers only when a test says so. Every ticket it hands out
	// is new, as LanguageServices's are.
	class FakeServer final : public DocumentServerPort
	{
	public:
		struct Asked
		{
			std::string What;
			std::filesystem::path Path;
			Ticket For{};
		};

		void EnsureStarted(const std::filesystem::path& path, LanguageId) override
		{
			Started.push_back(path);
		}

		void OpenDocument(const std::filesystem::path&, LanguageId, std::string) override {}
		void ChangeDocument(const std::filesystem::path&, LanguageId, std::string) override {}
		void SaveDocument(const std::filesystem::path&, LanguageId) override {}
		void CloseDocument(const std::filesystem::path&, LanguageId) override {}

		ServerStatus StatusFor(const std::filesystem::path&, LanguageId) override
		{
			ServerStatus status;
			status.State = State;
			return status;
		}

		Ticket AskDefinition(const std::filesystem::path& path, LanguageId,
							 const FilePosition&) override
		{
			return Record("definition", path);
		}

		Ticket AskReferences(const std::filesystem::path& path, LanguageId,
							 const FilePosition&) override
		{
			return Record("references", path);
		}

		Ticket AskSymbols(const std::filesystem::path& path, LanguageId) override
		{
			return Record("symbols", path);
		}

		Ticket AskSemanticTokens(const std::filesystem::path& path, LanguageId) override
		{
			return Record("semantic", path);
		}

		void Cancel(Ticket ticket) override { Cancelled.push_back(ticket); }

		void TakeAnswers(std::vector<Answer>& answers) override
		{
			answers.insert(answers.end(), Queued.begin(), Queued.end());
			Queued.clear();
		}

		// Queues an answer for the next Collect.
		void Reply(Ticket ticket, bool failed = false)
		{
			LanguageServices::Answer answer;
			answer.For = ticket;
			answer.Failed = failed;
			Queued.push_back(std::move(answer));
		}

		std::size_t CountOf(const std::string& what) const
		{
			return static_cast<std::size_t>(std::count_if(Asks.begin(), Asks.end(),
														  [&what](const Asked& asked) { return asked.What == what; }));
		}

		// The ticket of the most recent question of this kind.
		Ticket LastOf(const std::string& what) const
		{
			for (auto it = Asks.rbegin(); it != Asks.rend(); ++it)
			{
				if (it->What == what)
				{
					return it->For;
				}
			}

			FAIL("nothing of that kind was asked");
			return Ticket{};
		}

		bool WasCancelled(Ticket ticket) const
		{
			return std::find(Cancelled.begin(), Cancelled.end(), ticket) != Cancelled.end();
		}

		ServerState State = ServerState::Ready;
		std::vector<Asked> Asks;
		std::vector<Ticket> Cancelled;
		std::vector<std::filesystem::path> Started;
		std::vector<LanguageServices::Answer> Queued;

	private:
		Ticket Record(const char* what, const std::filesystem::path& path)
		{
			const Ticket ticket = static_cast<Ticket>(++m_Next);
			Asks.push_back({what, path, ticket});
			return ticket;
		}

		std::uint64_t m_Next = 0;
	};

	// Records what was routed to it. Takes symbols unless told the document cannot.
	class FakeTarget final : public LspAnswerTarget
	{
	public:
		void ApplyDefinition(const DefinitionQuestion& question, const Answer& answer) override
		{
			Definitions.push_back(question);
			DefinitionFailed.push_back(answer.Failed);
		}

		void ApplyReferences(const std::string& symbol, const Answer&) override
		{
			References.push_back(symbol);
		}

		void ApplySemanticTokens(const std::filesystem::path& path, const Answer&) override
		{
			Semantic.push_back(path);
		}

		bool ApplySymbols(const std::filesystem::path& path, const Answer&) override
		{
			Symbols.push_back(path);
			return TakesSymbols;
		}

		void ApplyCount(const std::filesystem::path&, std::size_t documentRow, std::size_t) override
		{
			Counts.push_back(documentRow);
		}

		bool Offer(const Answer&) override
		{
			++Offered;
			return false;
		}

		bool TakesSymbols = true;
		std::vector<DefinitionQuestion> Definitions;
		std::vector<bool> DefinitionFailed;
		std::vector<std::string> References;
		std::vector<std::filesystem::path> Semantic;
		std::vector<std::filesystem::path> Symbols;
		std::vector<std::size_t> Counts;
		std::size_t Offered = 0;
	};

	struct Harness
	{
		FakeServer Server;
		FakeTarget Target;
		double Now = 100.0;
		LspAnswerRouter Router{Server, [this] { return Now; }};
	};

	const std::filesystem::path kSource = "C:/project/main.cpp";

	DefinitionQuestion QuestionAt(std::size_t caretOffset)
	{
		DefinitionQuestion question;
		question.Path = kSource;
		question.CaretOffset = caretOffset;
		question.From.Path = kSource;
		return question;
	}
}

TEST_CASE("A second definition question cancels the first and only its own answer is applied")
{
	Harness h;

	h.Router.AskDefinition(QuestionAt(10));
	const Ticket first = h.Server.LastOf("definition");

	h.Router.AskDefinition(QuestionAt(20));
	const Ticket second = h.Server.LastOf("definition");

	CHECK(h.Server.WasCancelled(first));
	CHECK_FALSE(h.Server.WasCancelled(second));

	// The server may still answer the first: cancelling is a request, not a guarantee.
	h.Server.Reply(first);
	h.Server.Reply(second);
	h.Router.Collect(h.Target);

	REQUIRE(h.Target.Definitions.size() == 1);
	CHECK(h.Target.Definitions[0].CaretOffset == 20);
}

TEST_CASE("A definition question starts the server serving it before asking")
{
	Harness h;

	h.Router.AskDefinition(QuestionAt(0));

	REQUIRE(h.Server.Started.size() == 1);
	CHECK(h.Server.Started[0] == kSource);
}

TEST_CASE("A failed definition answer still reaches the target, so it can say there is none")
{
	Harness h;

	h.Router.AskDefinition(QuestionAt(0));
	h.Server.Reply(h.Server.LastOf("definition"), true);
	h.Router.Collect(h.Target);

	REQUIRE(h.Target.DefinitionFailed.size() == 1);
	CHECK(h.Target.DefinitionFailed[0]);
}

TEST_CASE("A second references question supersedes the first")
{
	Harness h;

	h.Router.AskReferences(kSource, FilePosition{}, "Old");
	const Ticket first = h.Server.LastOf("references");

	h.Router.AskReferences(kSource, FilePosition{}, "New");
	const Ticket second = h.Server.LastOf("references");

	CHECK(h.Server.WasCancelled(first));

	h.Server.Reply(first);
	h.Server.Reply(second);
	h.Router.Collect(h.Target);

	REQUIRE(h.Target.References.size() == 1);
	CHECK(h.Target.References[0] == "New");
}

TEST_CASE("A followed document is analysing until its declarations and colours have answered")
{
	Harness h;

	h.Router.Track(kSource);
	CHECK(h.Router.IsAnalysing(kSource));

	h.Router.AskSymbolsIfDue(kSource);
	REQUIRE(h.Server.CountOf("symbols") == 1);
	REQUIRE(h.Server.CountOf("semantic") == 1);

	h.Server.Reply(h.Server.LastOf("symbols"));
	h.Router.Collect(h.Target);

	// The colours are still outstanding.
	CHECK(h.Router.IsAnalysing(kSource));

	h.Server.Reply(h.Server.LastOf("semantic"));
	h.Router.Collect(h.Target);

	CHECK_FALSE(h.Router.IsAnalysing(kSource));
	CHECK(h.Target.Semantic.size() == 1);
}

TEST_CASE("A failed colours answer clears analysing without applying anything")
{
	Harness h;

	h.Router.Track(kSource);
	h.Router.AskSymbolsIfDue(kSource);

	h.Server.Reply(h.Server.LastOf("symbols"));
	h.Server.Reply(h.Server.LastOf("semantic"), true);
	h.Router.Collect(h.Target);

	CHECK_FALSE(h.Router.IsAnalysing(kSource));
	CHECK(h.Target.Semantic.empty());
}

TEST_CASE("A colours ticket the server refused at once does not hold analysing")
{
	// What LanguageServices does with a semantic-tokens question asked before the handshake: the
	// ticket is answered Failed on the very next drain, with no server round trip behind it.
	Harness h;

	h.Router.AskSemanticTokens(kSource);
	CHECK(h.Router.IsAnalysing(kSource));

	h.Server.Reply(h.Server.LastOf("semantic"), true);
	h.Router.Collect(h.Target);

	CHECK_FALSE(h.Router.IsAnalysing(kSource));
}

TEST_CASE("Failed declarations stay wanted and are asked for again only once the retry clock runs out")
{
	Harness h;

	h.Router.Track(kSource);
	h.Router.AskSymbolsIfDue(kSource);

	h.Server.Reply(h.Server.LastOf("symbols"), true);
	h.Router.Collect(h.Target);

	CHECK(h.Target.Symbols.empty());
	CHECK(h.Router.IsAnalysing(kSource));

	h.Router.AskSymbolsIfDue(kSource);
	CHECK(h.Server.CountOf("symbols") == 1);

	h.Now += LspAnswerRouter::kSymbolsRetrySeconds - 0.01;
	h.Router.AskSymbolsIfDue(kSource);
	CHECK(h.Server.CountOf("symbols") == 1);

	h.Now += 0.01;
	h.Router.AskSymbolsIfDue(kSource);
	CHECK(h.Server.CountOf("symbols") == 2);
}

TEST_CASE("One declarations question is outstanding at a time")
{
	Harness h;

	h.Router.Track(kSource);
	h.Router.AskSymbolsIfDue(kSource);

	h.Now += 60.0;
	h.Router.AskSymbolsIfDue(kSource);

	CHECK(h.Server.CountOf("symbols") == 1);
}

TEST_CASE("Declarations a document could not take stay wanted")
{
	Harness h;
	h.Target.TakesSymbols = false;

	h.Router.Track(kSource);
	h.Router.AskSymbolsIfDue(kSource);

	h.Server.Reply(h.Server.LastOf("symbols"));
	h.Server.Reply(h.Server.LastOf("semantic"));
	h.Router.Collect(h.Target);

	CHECK(h.Router.IsAnalysing(kSource));
}

TEST_CASE("An edit asks for declarations again at once rather than on the retry clock")
{
	Harness h;

	h.Router.Track(kSource);
	h.Router.AskSymbolsIfDue(kSource);
	h.Server.Reply(h.Server.LastOf("symbols"));
	h.Router.Collect(h.Target);

	h.Router.WantSymbolsNow(kSource);
	h.Router.AskSymbolsIfDue(kSource);

	CHECK(h.Server.CountOf("symbols") == 2);
}

TEST_CASE("A restarted server is asked for everything again and the counts in flight are dropped")
{
	Harness h;

	h.Router.Track(kSource);
	CHECK_FALSE(h.Router.NoteServerState(kSource));

	h.Router.AskSymbolsIfDue(kSource);
	h.Server.Reply(h.Server.LastOf("symbols"));
	h.Server.Reply(h.Server.LastOf("semantic"));
	h.Router.Collect(h.Target);
	REQUIRE_FALSE(h.Router.IsAnalysing(kSource));

	h.Router.AskCount(kSource, 7, FilePosition{});
	const Ticket count = h.Server.LastOf("references");

	// The process died and LanguageServices replaced it.
	h.Server.State = ServerState::Starting;
	CHECK(h.Router.NoteServerState(kSource));
	CHECK(h.Server.WasCancelled(count));

	// Asked again straight away - the retry clock has not moved.
	h.Router.AskSymbolsIfDue(kSource);
	CHECK(h.Server.CountOf("symbols") == 2);

	// And once the handshake completes, the colours too: a question asked before it is refused.
	const std::size_t coloursBefore = h.Server.CountOf("semantic");

	h.Server.State = ServerState::Ready;
	CHECK_FALSE(h.Router.NoteServerState(kSource));
	CHECK(h.Server.CountOf("semantic") == coloursBefore + 1);

	// The dead client's answer to the dropped count is not applied to a row.
	h.Server.Reply(count);
	h.Router.Collect(h.Target);
	CHECK(h.Target.Counts.empty());
}

TEST_CASE("A server state that has not changed asks nothing")
{
	Harness h;

	h.Router.Track(kSource);
	h.Router.NoteServerState(kSource);
	h.Router.NoteServerState(kSource);

	CHECK(h.Server.Asks.empty());
}

TEST_CASE("A count answer is applied to the row it was asked for, and a failed one to none")
{
	Harness h;

	h.Router.AskCount(kSource, 3, FilePosition{});
	const Ticket answered = h.Server.LastOf("references");

	h.Router.AskCount(kSource, 4, FilePosition{});
	const Ticket failed = h.Server.LastOf("references");

	h.Server.Reply(answered);
	h.Server.Reply(failed, true);
	h.Router.Collect(h.Target);

	REQUIRE(h.Target.Counts.size() == 1);
	CHECK(h.Target.Counts[0] == 3);
}

TEST_CASE("A forgotten document's outstanding answers are applied to nothing")
{
	Harness h;

	h.Router.Track(kSource);
	h.Router.AskSymbolsIfDue(kSource);
	h.Router.AskCount(kSource, 2, FilePosition{});

	h.Router.Forget(kSource);
	CHECK_FALSE(h.Router.IsAnalysing(kSource));

	for (const FakeServer::Asked& asked : h.Server.Asks)
	{
		h.Server.Reply(asked.For);
	}

	h.Router.Collect(h.Target);

	CHECK(h.Target.Symbols.empty());
	CHECK(h.Target.Semantic.empty());
	CHECK(h.Target.Counts.empty());
}

TEST_CASE("An answer the router did not ask for is offered to whoever did")
{
	Harness h;

	h.Server.Reply(static_cast<Ticket>(999));
	h.Router.Collect(h.Target);

	CHECK(h.Target.Offered == 1);
}
