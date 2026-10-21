#include <doctest/doctest.h>

#include "Editor/Server/LspAnswerRouter.h"
#include "Language/Registry/LanguageRegistry.h"

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

	constexpr double kStartSeconds = 100.0;

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

		bool HasChosenServer(LanguageId language) override { return LanguageHasServers(language); }
		bool OffersSwitchSourceHeader(LanguageId language) override { return language == LanguageId::Cpp; }

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

		Ticket AskHover(const std::filesystem::path& path, LanguageId, const FilePosition&) override
		{
			return Record("hover", path);
		}

		Ticket AskSwitchSourceHeader(const std::filesystem::path& path, LanguageId) override
		{
			return Record("switch", path);
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

		void TakeRecolour(std::vector<std::filesystem::path>& paths) override
		{
			paths.insert(paths.end(), Recolour.begin(), Recolour.end());
			Recolour.clear();
		}

		void Reply(Ticket ticket, bool failed = false)
		{
			LanguageServices::Answer answer;
			answer.For = ticket;
			answer.Failed = failed;
			Queued.push_back(std::move(answer));
		}

		std::size_t CountOf(const std::string& what) const
		{
			std::size_t count = 0;

			for (const Asked& asked : Asks)
			{
				if (asked.What == what)
				{
					++count;
				}
			}

			return count;
		}

		Ticket LastOf(const std::string& what) const
		{
			for (std::vector<Asked>::const_reverse_iterator asked = Asks.rbegin(); asked != Asks.rend(); ++asked)
			{
				if (asked->What == what)
				{
					return asked->For;
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
		std::vector<std::filesystem::path> Recolour;

	private:
		Ticket Record(const char* what, const std::filesystem::path& path)
		{
			const Ticket ticket = static_cast<Ticket>(++m_Next);
			Asks.push_back({what, path, ticket});
			return ticket;
		}

		std::uint64_t m_Next = 0;
	};

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

		void ApplySwitchSourceHeader(const std::filesystem::path& from, const Answer&) override
		{
			Switched.push_back(from);
		}

		void ApplyHover(const HoverQuestion& question, const Answer&) override
		{
			Hovers.push_back(question);
		}

		void ApplyPreviewDefinition(const HoverQuestion& question, const Answer&) override
		{
			Previews.push_back(question);
		}

		std::vector<HoverQuestion> Hovers;
		std::vector<HoverQuestion> Previews;

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

		void FailCount(const std::filesystem::path&, std::size_t documentRow) override
		{
			FailedCounts.push_back(documentRow);
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
		std::vector<std::filesystem::path> Switched;
		std::vector<std::filesystem::path> Semantic;
		std::vector<std::filesystem::path> Symbols;
		std::vector<std::size_t> Counts;
		std::vector<std::size_t> FailedCounts;
		std::size_t Offered = 0;
	};

	struct Harness
	{
		FakeServer Server;
		FakeTarget Target;
		double Now = kStartSeconds;
		LspAnswerRouter Router{Server, [this] { return Now; }};
	};

	const std::filesystem::path kSource = "C:/project/main.cpp";
	const FilePosition kUnimportantPosition{};
	constexpr bool kFailedAnswer = true;
	constexpr double kOneHundredthSecond = 0.01;
	constexpr double kLongIdleSeconds = 60.0;
	constexpr Ticket kTicketNobodyAsked = static_cast<Ticket>(999);

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
	Harness harness;

	harness.Router.AskDefinition(QuestionAt(10));
	const Ticket first = harness.Server.LastOf("definition");

	harness.Router.AskDefinition(QuestionAt(20));
	const Ticket second = harness.Server.LastOf("definition");

	CHECK(harness.Server.WasCancelled(first));
	CHECK_FALSE(harness.Server.WasCancelled(second));

	harness.Server.Reply(first);
	harness.Server.Reply(second);
	harness.Router.Collect(harness.Target);

	REQUIRE(harness.Target.Definitions.size() == 1);
	CHECK(harness.Target.Definitions[0].CaretOffset == 20);
}

TEST_CASE("A definition question starts the server serving it before asking")
{
	Harness harness;

	harness.Router.AskDefinition(QuestionAt(0));

	REQUIRE(harness.Server.Started.size() == 1);
	CHECK(harness.Server.Started[0] == kSource);
}

TEST_CASE("A failed definition answer still reaches the target, so it can say there is none")
{
	Harness harness;

	harness.Router.AskDefinition(QuestionAt(0));
	harness.Server.Reply(harness.Server.LastOf("definition"), kFailedAnswer);
	harness.Router.Collect(harness.Target);

	REQUIRE(harness.Target.DefinitionFailed.size() == 1);
	CHECK(harness.Target.DefinitionFailed[0]);
}

TEST_CASE("A second references question supersedes the first")
{
	Harness harness;

	harness.Router.AskReferences(kSource, kUnimportantPosition, "Old");
	const Ticket first = harness.Server.LastOf("references");

	harness.Router.AskReferences(kSource, kUnimportantPosition, "New");
	const Ticket second = harness.Server.LastOf("references");

	CHECK(harness.Server.WasCancelled(first));

	harness.Server.Reply(first);
	harness.Server.Reply(second);
	harness.Router.Collect(harness.Target);

	REQUIRE(harness.Target.References.size() == 1);
	CHECK(harness.Target.References[0] == "New");
}

TEST_CASE("A followed document is analysing until its declarations and colours have answered")
{
	Harness harness;

	harness.Router.Track(kSource);
	CHECK(harness.Router.IsAnalysing(kSource));

	harness.Router.AskSymbolsIfDue(kSource);
	REQUIRE(harness.Server.CountOf("symbols") == 1);
	REQUIRE(harness.Server.CountOf("semantic") == 1);

	harness.Server.Reply(harness.Server.LastOf("symbols"));
	harness.Router.Collect(harness.Target);

	CHECK(harness.Router.IsAnalysing(kSource));

	harness.Server.Reply(harness.Server.LastOf("semantic"));
	harness.Router.Collect(harness.Target);

	CHECK_FALSE(harness.Router.IsAnalysing(kSource));
	CHECK(harness.Target.Semantic.size() == 1);
}

TEST_CASE("A followed document the server recolours is asked for its colours again and others are not")
{
	Harness harness;
	harness.Router.Track(kSource);

	harness.Server.Recolour = {kSource, kSource, "C:/project/closed.cpp"};
	harness.Router.Collect(harness.Target);

	REQUIRE(harness.Server.CountOf("semantic") == 1);
	CHECK(harness.Server.Asks.back().Path == kSource);

	harness.Server.Reply(harness.Server.LastOf("semantic"));
	harness.Router.Collect(harness.Target);

	REQUIRE(harness.Target.Semantic.size() == 1);
	CHECK(harness.Target.Semantic[0] == kSource);
}

TEST_CASE("A failed colours answer clears analysing without applying anything")
{
	Harness harness;

	harness.Router.Track(kSource);
	harness.Router.AskSymbolsIfDue(kSource);

	harness.Server.Reply(harness.Server.LastOf("symbols"));
	harness.Server.Reply(harness.Server.LastOf("semantic"), kFailedAnswer);
	harness.Router.Collect(harness.Target);

	CHECK_FALSE(harness.Router.IsAnalysing(kSource));
	CHECK(harness.Target.Semantic.empty());
}

TEST_CASE("A colours ticket the server refused at once does not hold analysing")
{
	Harness harness;

	harness.Router.AskSemanticTokens(kSource);
	CHECK(harness.Router.IsAnalysing(kSource));

	harness.Server.Reply(harness.Server.LastOf("semantic"), kFailedAnswer);
	harness.Router.Collect(harness.Target);

	CHECK_FALSE(harness.Router.IsAnalysing(kSource));
}

TEST_CASE("Failed declarations stay wanted and are asked for again only once the retry clock runs out")
{
	Harness harness;

	harness.Router.Track(kSource);
	harness.Router.AskSymbolsIfDue(kSource);

	harness.Server.Reply(harness.Server.LastOf("symbols"), kFailedAnswer);
	harness.Router.Collect(harness.Target);

	CHECK(harness.Target.Symbols.empty());
	CHECK(harness.Router.IsAnalysing(kSource));

	harness.Router.AskSymbolsIfDue(kSource);
	CHECK(harness.Server.CountOf("symbols") == 1);

	harness.Now += kFailedRequestRetrySeconds - kOneHundredthSecond;
	harness.Router.AskSymbolsIfDue(kSource);
	CHECK(harness.Server.CountOf("symbols") == 1);

	harness.Now += kOneHundredthSecond;
	harness.Router.AskSymbolsIfDue(kSource);
	CHECK(harness.Server.CountOf("symbols") == 2);
}

TEST_CASE("One declarations question is outstanding at a time")
{
	Harness harness;

	harness.Router.Track(kSource);
	harness.Router.AskSymbolsIfDue(kSource);

	harness.Now += kLongIdleSeconds;
	harness.Router.AskSymbolsIfDue(kSource);

	CHECK(harness.Server.CountOf("symbols") == 1);
}

TEST_CASE("A late failure of an older declarations question leaves the newer one outstanding")
{
	Harness harness;

	harness.Router.Track(kSource);
	CHECK_FALSE(harness.Router.NoteServerState(kSource));

	harness.Router.AskSymbolsIfDue(kSource);
	const Ticket older = harness.Server.LastOf("symbols");

	harness.Server.State = ServerState::Starting;
	REQUIRE(harness.Router.NoteServerState(kSource));

	harness.Router.AskSymbolsIfDue(kSource);
	REQUIRE(harness.Server.CountOf("symbols") == 2);

	harness.Server.Reply(older, kFailedAnswer);
	harness.Router.Collect(harness.Target);

	harness.Now += kFailedRequestRetrySeconds;
	harness.Router.AskSymbolsIfDue(kSource);

	CHECK(harness.Server.CountOf("symbols") == 2);
}

TEST_CASE("Declarations a document could not take stay wanted")
{
	Harness harness;
	harness.Target.TakesSymbols = false;

	harness.Router.Track(kSource);
	harness.Router.AskSymbolsIfDue(kSource);

	harness.Server.Reply(harness.Server.LastOf("symbols"));
	harness.Server.Reply(harness.Server.LastOf("semantic"));
	harness.Router.Collect(harness.Target);

	CHECK(harness.Router.IsAnalysing(kSource));
}

TEST_CASE("An edit asks for declarations again at once rather than on the retry clock")
{
	Harness harness;

	harness.Router.Track(kSource);
	harness.Router.AskSymbolsIfDue(kSource);
	harness.Server.Reply(harness.Server.LastOf("symbols"));
	harness.Router.Collect(harness.Target);

	harness.Router.WantSymbolsNow(kSource);
	harness.Router.AskSymbolsIfDue(kSource);

	CHECK(harness.Server.CountOf("symbols") == 2);
}

TEST_CASE("Declarations wanted while a question is outstanding are asked for again once it answers")
{
	Harness harness;

	harness.Router.Track(kSource);
	harness.Router.AskSymbolsIfDue(kSource);

	harness.Router.WantSymbolsNow(kSource);
	harness.Router.AskSymbolsIfDue(kSource);
	REQUIRE(harness.Server.CountOf("symbols") == 1);

	harness.Server.Reply(harness.Server.LastOf("symbols"));
	harness.Router.Collect(harness.Target);
	harness.Router.AskSymbolsIfDue(kSource);

	CHECK(harness.Server.CountOf("symbols") == 2);

	harness.Server.Reply(harness.Server.LastOf("symbols"));
	harness.Router.Collect(harness.Target);
	harness.Router.AskSymbolsIfDue(kSource);

	CHECK(harness.Server.CountOf("symbols") == 2);
}

TEST_CASE("A restarted server is asked for everything again and the counts in flight are dropped")
{
	Harness harness;

	harness.Router.Track(kSource);
	CHECK_FALSE(harness.Router.NoteServerState(kSource));

	harness.Router.AskSymbolsIfDue(kSource);
	harness.Server.Reply(harness.Server.LastOf("symbols"));
	harness.Server.Reply(harness.Server.LastOf("semantic"));
	harness.Router.Collect(harness.Target);
	REQUIRE_FALSE(harness.Router.IsAnalysing(kSource));

	harness.Router.AskCount(kSource, 7, kUnimportantPosition);
	const Ticket count = harness.Server.LastOf("references");

	harness.Server.State = ServerState::Starting;
	CHECK(harness.Router.NoteServerState(kSource));
	CHECK(harness.Server.WasCancelled(count));

	harness.Router.AskSymbolsIfDue(kSource);
	CHECK(harness.Server.CountOf("symbols") == 2);

	const std::size_t coloursBefore = harness.Server.CountOf("semantic");

	harness.Server.State = ServerState::Ready;
	CHECK_FALSE(harness.Router.NoteServerState(kSource));
	CHECK(harness.Server.CountOf("semantic") == coloursBefore + 1);

	harness.Server.Reply(count);
	harness.Router.Collect(harness.Target);
	CHECK(harness.Target.Counts.empty());
}

TEST_CASE("A server state that has not changed asks nothing")
{
	Harness harness;

	harness.Router.Track(kSource);
	harness.Router.NoteServerState(kSource);
	harness.Router.NoteServerState(kSource);

	CHECK(harness.Server.Asks.empty());
}

TEST_CASE("References are counted only by a server that has finished starting and indexing")
{
	Harness harness;

	harness.Server.State = ServerState::Starting;
	CHECK_FALSE(harness.Router.CanCountReferences(kSource));

	harness.Server.State = ServerState::Indexing;
	CHECK_FALSE(harness.Router.CanCountReferences(kSource));

	harness.Server.State = ServerState::Ready;
	CHECK(harness.Router.CanCountReferences(kSource));

	harness.Server.State = ServerState::ProjectIncomplete;
	CHECK(harness.Router.CanCountReferences(kSource));

	harness.Server.State = ServerState::Failed;
	CHECK_FALSE(harness.Router.CanCountReferences(kSource));
}

TEST_CASE("A count answer is applied to the row it was asked for, and a failed one is reported for its row")
{
	Harness harness;

	harness.Router.AskCount(kSource, 3, kUnimportantPosition);
	const Ticket answered = harness.Server.LastOf("references");

	harness.Router.AskCount(kSource, 4, kUnimportantPosition);
	const Ticket failed = harness.Server.LastOf("references");

	harness.Server.Reply(answered);
	harness.Server.Reply(failed, kFailedAnswer);
	harness.Router.Collect(harness.Target);

	REQUIRE(harness.Target.Counts.size() == 1);
	CHECK(harness.Target.Counts[0] == 3);
	CHECK(harness.Target.FailedCounts == std::vector<std::size_t>{4});
}

TEST_CASE("A forgotten document's outstanding answers are applied to nothing")
{
	Harness harness;

	harness.Router.Track(kSource);
	harness.Router.AskSymbolsIfDue(kSource);
	harness.Router.AskCount(kSource, 2, kUnimportantPosition);

	harness.Router.Forget(kSource);
	CHECK_FALSE(harness.Router.IsAnalysing(kSource));

	for (const FakeServer::Asked& asked : harness.Server.Asks)
	{
		harness.Server.Reply(asked.For);
	}

	harness.Router.Collect(harness.Target);

	CHECK(harness.Target.Symbols.empty());
	CHECK(harness.Target.Semantic.empty());
	CHECK(harness.Target.Counts.empty());
}

TEST_CASE("An answer the router did not ask for is offered to whoever did")
{
	Harness harness;

	harness.Server.Reply(kTicketNobodyAsked);
	harness.Router.Collect(harness.Target);

	CHECK(harness.Target.Offered == 1);
}

TEST_CASE("A second switch header/source question supersedes the first")
{
	Harness harness;

	harness.Router.AskSwitchSourceHeader(kSource);
	const Ticket first = harness.Server.LastOf("switch");

	harness.Router.AskSwitchSourceHeader(kSource);
	const Ticket second = harness.Server.LastOf("switch");

	CHECK(harness.Server.WasCancelled(first));

	harness.Server.Reply(first);
	harness.Server.Reply(second);
	harness.Router.Collect(harness.Target);

	REQUIRE(harness.Target.Switched.size() == 1);
	CHECK(harness.Target.Switched[0] == kSource);
}

TEST_CASE("A switch header/source question starts the server before asking")
{
	Harness harness;

	harness.Router.AskSwitchSourceHeader(kSource);

	REQUIRE(harness.Server.Started.size() == 1);
	CHECK(harness.Server.Started[0] == kSource);
}

namespace {
	HoverQuestion HoverAt(std::size_t from)
	{
		HoverQuestion question;
		question.Path = kSource;
		question.TokenFrom = from;
		question.TokenTo = from + 3;
		question.From.Path = kSource;
		return question;
	}
}

TEST_CASE("A hover never cancels a pending F12, and F12 never cancels a hover")
{
	Harness harness;

	harness.Router.AskDefinition(QuestionAt(5));
	const Ticket definitionTicket = harness.Server.LastOf("definition");

	harness.Router.AskHover(HoverAt(1));
	const Ticket hover = harness.Server.LastOf("hover");

	harness.Router.AskPreviewDefinition(HoverAt(1));
	const Ticket preview = harness.Server.LastOf("definition");

	CHECK_FALSE(harness.Server.WasCancelled(definitionTicket));
	CHECK_FALSE(harness.Server.WasCancelled(hover));

	harness.Server.Reply(definitionTicket);
	harness.Server.Reply(hover);
	harness.Server.Reply(preview);
	harness.Router.Collect(harness.Target);

	CHECK(harness.Target.Definitions.size() == 1);
	CHECK(harness.Target.Hovers.size() == 1);
	CHECK(harness.Target.Previews.size() == 1);
}

TEST_CASE("A second hover supersedes the first")
{
	Harness harness;

	harness.Router.AskHover(HoverAt(1));
	const Ticket first = harness.Server.LastOf("hover");
	harness.Router.AskHover(HoverAt(9));
	const Ticket second = harness.Server.LastOf("hover");

	CHECK(harness.Server.WasCancelled(first));

	harness.Server.Reply(first);
	harness.Server.Reply(second);
	harness.Router.Collect(harness.Target);

	REQUIRE(harness.Target.Hovers.size() == 1);
	CHECK(harness.Target.Hovers[0].TokenFrom == 9);
}

TEST_CASE("CancelHover cancels both hover slots and drops their answers")
{
	Harness harness;

	harness.Router.AskHover(HoverAt(1));
	const Ticket hover = harness.Server.LastOf("hover");
	harness.Router.AskPreviewDefinition(HoverAt(1));
	const Ticket preview = harness.Server.LastOf("definition");

	harness.Router.CancelHover(kSource);

	CHECK(harness.Server.WasCancelled(hover));
	CHECK(harness.Server.WasCancelled(preview));

	harness.Server.Reply(hover);
	harness.Server.Reply(preview);
	harness.Router.Collect(harness.Target);

	CHECK(harness.Target.Hovers.empty());
	CHECK(harness.Target.Previews.empty());
}

TEST_CASE("A hover cancelled for one document leaves another document's questions alone")
{
	Harness harness;

	harness.Router.AskHover(HoverAt(1));
	const Ticket hover = harness.Server.LastOf("hover");
	harness.Router.AskPreviewDefinition(HoverAt(1));
	const Ticket preview = harness.Server.LastOf("definition");

	harness.Router.CancelHover("C:/project/other.cpp");

	CHECK_FALSE(harness.Server.WasCancelled(hover));
	CHECK_FALSE(harness.Server.WasCancelled(preview));

	harness.Server.Reply(hover);
	harness.Server.Reply(preview);
	harness.Router.Collect(harness.Target);

	CHECK(harness.Target.Hovers.size() == 1);
	CHECK(harness.Target.Previews.size() == 1);
}
