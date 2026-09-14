#include <doctest/doctest.h>

#include "Lang/Analysis/WorkspaceAnalysis.h"

#include "Editor/AnalysisCache.h"

#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

using namespace Graphite;

namespace {

	// Records what it was told instead of talking to a process. Every claim this feature makes
	// about a server — one file open at a time, none for a file with a tab, one didClose for
	// every didOpen — is a claim about this log.
	class FakePort final : public WorkspaceAnalysisPort
	{
	public:
		struct Call
		{
			std::string What;
			std::filesystem::path Path;
		};

		void Open(const std::filesystem::path& path, LanguageId, std::string) override
		{
			Calls.push_back({"open", path});
		}

		LanguageServices::Ticket AskSemanticTokens(const std::filesystem::path& path,
												   LanguageId) override
		{
			Calls.push_back({"colours", path});

			return static_cast<LanguageServices::Ticket>(++Tickets);
		}

		LanguageServices::Ticket AskSymbols(const std::filesystem::path& path, LanguageId) override
		{
			Calls.push_back({"symbols", path});

			return static_cast<LanguageServices::Ticket>(++Tickets);
		}

		void Close(const std::filesystem::path& path, LanguageId) override
		{
			Calls.push_back({"close", path});
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

		// Replays the numbering AskSemanticTokens and AskSymbols hand out, so a test can name the
		// ticket a question got without the class under test having to tell it.
		LanguageServices::Ticket TicketOf(const std::string& what,
										  const std::filesystem::path& path) const
		{
			std::uint64_t ticket = 0;

			for (const Call& call : Calls)
			{
				if (call.What == "colours" || call.What == "symbols")
				{
					++ticket;

					if (call.What == what && call.Path == path)
					{
						return static_cast<LanguageServices::Ticket>(ticket);
					}
				}
			}

			return static_cast<LanguageServices::Ticket>(0);
		}

		std::vector<Call> Calls;
		std::uint64_t Tickets = 0;
	};

	// Answers every request at once and out of the same frame, so a test can drive a whole flight
	// without a thread or a wait.
	class FakeTexts final : public FileTextSource
	{
	public:
		void Request(const std::filesystem::path& path) override
		{
			FileText text;
			text.Path = path;
			text.Text = "int x;\n";
			text.ModifiedAt = std::filesystem::file_time_type{} + std::chrono::seconds(100);
			text.Readable = Readable;

			Pending.push_back(std::move(text));
		}

		void Take(std::vector<FileText>& out) override
		{
			for (FileText& text : Pending)
			{
				out.push_back(std::move(text));
			}

			Pending.clear();
		}

		bool Readable = true;
		std::vector<FileText> Pending;
	};

	LanguageServices::Answer Colours(LanguageServices::Ticket ticket)
	{
		LanguageServices::Answer answer;
		answer.For = ticket;
		// Spelled the way SemanticOverlayTests and PreviewSemanticsTests spell it: Utf16Column's
		// constructor is explicit on purpose, so a bare number will not compile.
		answer.SemanticTokens.push_back(SemanticToken{0, Utf16Column(0), 3, SemanticKind::Type});

		return answer;
	}

	LanguageServices::Answer Symbols(LanguageServices::Ticket ticket)
	{
		LanguageServices::Answer answer;
		answer.For = ticket;

		Declaration declaration;
		declaration.Name = "x";
		declaration.Kind = DeclarationKind::Other;

		answer.Declarations.push_back(std::move(declaration));

		return answer;
	}

	LanguageServices::Answer Failed(LanguageServices::Ticket ticket)
	{
		LanguageServices::Answer answer;
		answer.For = ticket;
		answer.Failed = true;

		return answer;
	}

	const auto NoTabs = [](const std::filesystem::path&) { return false; };
	const auto EveryTab = [](const std::filesystem::path&) { return true; };

	// The stamp FakeTexts puts on every file it hands over. A function rather than an object at
	// namespace scope, per ADR 0033 on complex initialization of globals.
	std::filesystem::file_time_type OnDisk()
	{
		return std::filesystem::file_time_type{} + std::chrono::seconds(100);
	}
}

TEST_CASE("One file is opened at a time, however many are queued")
{
	FakePort port;
	FakeTexts texts;
	AnalysisCache cache{1024 * 1024};
	WorkspaceAnalysis analysis{port, texts, cache};

	analysis.Enqueue({"a.cpp", "b.cpp", "c.cpp"});

	analysis.Tick(1.0, false, NoTabs);   // asks for a.cpp's text
	analysis.Tick(2.0, false, NoTabs);   // the text arrives and a.cpp is opened
	analysis.Tick(3.0, false, NoTabs);   // nothing new while a.cpp is outstanding

	CHECK(port.CountOf("open") == 1);
	CHECK(port.Calls.front().Path == std::filesystem::path{"a.cpp"});
}

TEST_CASE("A finished file is closed, cached, and the next one starts")
{
	FakePort port;
	FakeTexts texts;
	AnalysisCache cache{1024 * 1024};
	WorkspaceAnalysis analysis{port, texts, cache};

	analysis.Enqueue({"a.cpp", "b.cpp"});

	analysis.Tick(1.0, false, NoTabs);
	analysis.Tick(2.0, false, NoTabs);

	CHECK(analysis.Apply(Colours(port.TicketOf("colours", "a.cpp"))));
	CHECK(analysis.Apply(Symbols(port.TicketOf("symbols", "a.cpp"))));

	// Retiring is a Tick's job, not an answer's: it needs the hasTab predicate, which only a Tick
	// is given. The same tick asks for b.cpp's text.
	analysis.Tick(3.0, false, NoTabs);

	CHECK(port.CountOf("close") == 1);

	const std::optional<CachedAnalysis> kept = cache.Take("a.cpp", OnDisk());

	REQUIRE(kept.has_value());
	CHECK_FALSE(kept->Semantic.IsEmpty());
	CHECK(kept->Declarations.size() == 1);

	analysis.Tick(4.0, false, NoTabs);

	CHECK(port.CountOf("open") == 2);
	CHECK(port.Calls.back().Path == std::filesystem::path{"b.cpp"});
}

TEST_CASE("Nothing new is launched while the open document is waiting on the server")
{
	FakePort port;
	FakeTexts texts;
	AnalysisCache cache{1024 * 1024};
	WorkspaceAnalysis analysis{port, texts, cache};

	analysis.Enqueue({"a.cpp"});

	analysis.Tick(1.0, true, NoTabs);
	analysis.Tick(2.0, true, NoTabs);

	CHECK(port.Calls.empty());

	analysis.Tick(3.0, false, NoTabs);
	analysis.Tick(4.0, false, NoTabs);

	CHECK(port.CountOf("open") == 1);
}

TEST_CASE("A flight already in the air is finished rather than abandoned when the editor gets busy")
{
	FakePort port;
	FakeTexts texts;
	AnalysisCache cache{1024 * 1024};
	WorkspaceAnalysis analysis{port, texts, cache};

	analysis.Enqueue({"a.cpp"});

	analysis.Tick(1.0, false, NoTabs);
	analysis.Tick(2.0, false, NoTabs);

	analysis.Tick(3.0, true, NoTabs);

	CHECK(port.CountOf("close") == 0);

	CHECK(analysis.Apply(Colours(port.TicketOf("colours", "a.cpp"))));
	CHECK(analysis.Apply(Symbols(port.TicketOf("symbols", "a.cpp"))));

	// Still busy, and the flight still retires: what was already in the air is seen through.
	analysis.Tick(4.0, true, NoTabs);

	CHECK(port.CountOf("close") == 1);
	CHECK(cache.Take("a.cpp", OnDisk()).has_value());
}

TEST_CASE("A file that already has a tab is never opened from here")
{
	FakePort port;
	FakeTexts texts;
	AnalysisCache cache{1024 * 1024};
	WorkspaceAnalysis analysis{port, texts, cache};

	analysis.Enqueue({"a.cpp"});

	analysis.Tick(1.0, false, EveryTab);
	analysis.Tick(2.0, false, EveryTab);

	CHECK(port.Calls.empty());
	CHECK(analysis.Progress().Done == 1);
}

TEST_CASE("A file that acquires a tab mid-flight is abandoned and nothing is cached")
{
	FakePort port;
	FakeTexts texts;
	AnalysisCache cache{1024 * 1024};
	WorkspaceAnalysis analysis{port, texts, cache};

	analysis.Enqueue({"a.cpp"});

	analysis.Tick(1.0, false, NoTabs);
	analysis.Tick(2.0, false, NoTabs);

	analysis.Apply(Colours(port.TicketOf("colours", "a.cpp")));
	analysis.Apply(Symbols(port.TicketOf("symbols", "a.cpp")));

	// The tab appeared before the answers were retired.
	analysis.Tick(3.0, false, EveryTab);

	CHECK_FALSE(cache.Take("a.cpp", OnDisk()).has_value());

	// And not closed either: the tab holds the document now, CloseDocument counts no references,
	// and a tab only ever opens one once. A didClose from here would take the file away from it
	// for good.
	CHECK(port.CountOf("close") == 0);

	// Dropped all the same — the queue is not left waiting on a flight nobody will retire.
	CHECK(analysis.Progress().Done == 1);
}

TEST_CASE("A flight that times out after a tab takes it over closes nothing")
{
	FakePort port;
	FakeTexts texts;
	AnalysisCache cache{1024 * 1024};
	WorkspaceAnalysis analysis{port, texts, cache};

	// One file, so the Done below is the timed-out flight's and nothing else's: a queue behind it
	// would be skipped in the same tick, every skip counting as done.
	analysis.Enqueue({"a.cpp"});

	analysis.Tick(1.0, false, NoTabs);
	analysis.Tick(2.0, false, NoTabs);

	// Never answered, and a tab appeared in the meantime.
	analysis.Tick(2.0 + 200.0, false, EveryTab);

	CHECK(port.CountOf("close") == 0);
	CHECK(analysis.Progress().Done == 1);
}

TEST_CASE("An answer nobody here asked for is left for somebody else")
{
	FakePort port;
	FakeTexts texts;
	AnalysisCache cache{1024 * 1024};
	WorkspaceAnalysis analysis{port, texts, cache};

	// Refused with nothing in the air, and refused again with a flight outstanding — the second
	// is what makes this a claim about matching tickets rather than about being idle.
	CHECK_FALSE(analysis.Apply(Colours(static_cast<LanguageServices::Ticket>(9999))));

	analysis.Enqueue({"a.cpp"});

	analysis.Tick(1.0, false, NoTabs);
	analysis.Tick(2.0, false, NoTabs);

	CHECK_FALSE(analysis.Apply(Colours(static_cast<LanguageServices::Ticket>(9999))));
	CHECK(analysis.Apply(Colours(port.TicketOf("colours", "a.cpp"))));
}

TEST_CASE("A failed request retires the file without caching it, and the queue moves on")
{
	FakePort port;
	FakeTexts texts;
	AnalysisCache cache{1024 * 1024};
	WorkspaceAnalysis analysis{port, texts, cache};

	analysis.Enqueue({"a.cpp", "b.cpp"});

	analysis.Tick(1.0, false, NoTabs);
	analysis.Tick(2.0, false, NoTabs);

	CHECK(analysis.Apply(Failed(port.TicketOf("colours", "a.cpp"))));
	CHECK(analysis.Apply(Failed(port.TicketOf("symbols", "a.cpp"))));

	analysis.Tick(3.0, false, NoTabs);

	CHECK_FALSE(cache.Take("a.cpp", OnDisk()).has_value());

	analysis.Tick(4.0, false, NoTabs);

	CHECK(port.CountOf("open") == 2);
	CHECK(port.Calls.back().Path == std::filesystem::path{"b.cpp"});
}

TEST_CASE("A file that cannot be read is dropped without being mentioned to a server")
{
	FakePort port;
	FakeTexts texts;
	texts.Readable = false;

	AnalysisCache cache{1024 * 1024};
	WorkspaceAnalysis analysis{port, texts, cache};

	analysis.Enqueue({"a.png"});

	analysis.Tick(1.0, false, NoTabs);
	analysis.Tick(2.0, false, NoTabs);

	CHECK(port.Calls.empty());
	CHECK(analysis.Progress().Done == 1);
}

TEST_CASE("A file in a language no server serves is dropped without being mentioned to one")
{
	FakePort port;
	FakeTexts texts;
	AnalysisCache cache{1024 * 1024};
	WorkspaceAnalysis analysis{port, texts, cache};

	analysis.Enqueue({"notes.txt"});

	analysis.Tick(1.0, false, NoTabs);
	analysis.Tick(2.0, false, NoTabs);

	CHECK(port.Calls.empty());
	CHECK(analysis.Progress().Done == 1);
}

TEST_CASE("A flight that is never answered is given up on rather than blocking the queue forever")
{
	FakePort port;
	FakeTexts texts;
	AnalysisCache cache{1024 * 1024};
	WorkspaceAnalysis analysis{port, texts, cache};

	analysis.Enqueue({"a.cpp", "b.cpp"});

	analysis.Tick(1.0, false, NoTabs);
	analysis.Tick(2.0, false, NoTabs);

	// Well past the timeout rather than exactly on it: a tick on the boundary would only be
	// asserting which way the comparison rounds.
	analysis.Tick(2.0 + 200.0, false, NoTabs);

	CHECK(port.CountOf("close") == 1);

	analysis.Tick(2.0 + 201.0, false, NoTabs);

	CHECK(port.CountOf("open") == 2);
	CHECK(port.Calls.back().Path == std::filesystem::path{"b.cpp"});
}

TEST_CASE("A flight answered inside the timeout is not given up on")
{
	FakePort port;
	FakeTexts texts;
	AnalysisCache cache{1024 * 1024};
	WorkspaceAnalysis analysis{port, texts, cache};

	analysis.Enqueue({"a.cpp"});

	analysis.Tick(1.0, false, NoTabs);
	analysis.Tick(2.0, false, NoTabs);

	analysis.Tick(2.0 + 60.0, false, NoTabs);

	CHECK(port.CountOf("close") == 0);

	CHECK(analysis.Apply(Colours(port.TicketOf("colours", "a.cpp"))));
	CHECK(analysis.Apply(Symbols(port.TicketOf("symbols", "a.cpp"))));

	analysis.Tick(2.0 + 61.0, false, NoTabs);

	CHECK(cache.Take("a.cpp", OnDisk()).has_value());
}

TEST_CASE("Disabled, it queues nothing and reports nothing")
{
	FakePort port;
	FakeTexts texts;
	AnalysisCache cache{1024 * 1024};
	WorkspaceAnalysis analysis{port, texts, cache};

	analysis.SetEnabled(false);
	analysis.Enqueue({"a.cpp", "b.cpp"});

	analysis.Tick(1.0, false, NoTabs);
	analysis.Tick(2.0, false, NoTabs);

	CHECK(port.Calls.empty());
	CHECK(analysis.Progress().Total == 0);
}

TEST_CASE("Progress counts what is done against what was queued")
{
	FakePort port;
	FakeTexts texts;
	AnalysisCache cache{1024 * 1024};
	WorkspaceAnalysis analysis{port, texts, cache};

	analysis.SetWalking(true);
	analysis.Enqueue({"a.cpp", "b.cpp", "c.cpp"});

	CHECK(analysis.Progress().Walking);
	CHECK(analysis.Progress().Total == 3);
	CHECK(analysis.Progress().Done == 0);

	analysis.SetWalking(false);
	analysis.Tick(1.0, false, NoTabs);
	analysis.Tick(2.0, false, NoTabs);
	analysis.Apply(Colours(port.TicketOf("colours", "a.cpp")));
	analysis.Apply(Symbols(port.TicketOf("symbols", "a.cpp")));
	analysis.Tick(3.0, false, NoTabs);

	CHECK_FALSE(analysis.Progress().Walking);
	CHECK(analysis.Progress().Done == 1);
	CHECK(analysis.Progress().Total == 3);
}

TEST_CASE("Closing the folder drops the queue and closes what was open")
{
	FakePort port;
	FakeTexts texts;
	AnalysisCache cache{1024 * 1024};
	WorkspaceAnalysis analysis{port, texts, cache};

	analysis.Enqueue({"a.cpp", "b.cpp"});

	analysis.Tick(1.0, false, NoTabs);
	analysis.Tick(2.0, false, NoTabs);

	analysis.Clear();

	CHECK(port.CountOf("close") == 1);
	CHECK(analysis.Progress().Total == 0);

	analysis.Tick(3.0, false, NoTabs);
	analysis.Tick(4.0, false, NoTabs);

	CHECK(port.CountOf("open") == 1);
}
