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
	// about a server — one file open at a time, none for a file somebody else already holds, one
	// didClose for every didOpen — is a claim about this log.
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

	// The `heldElsewhere` predicate: whether somebody other than this class already has the file
	// open with the server. A tab is one such holder and a preview pane is another, which is why
	// these are named after the answer rather than after tabs.
	const auto NobodyElse = [](const std::filesystem::path&) { return false; };
	const auto SomebodyElse = [](const std::filesystem::path&) { return true; };

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

	analysis.Tick(1.0, false, NobodyElse);   // asks for a.cpp's text
	analysis.Tick(2.0, false, NobodyElse);   // the text arrives and a.cpp is opened
	analysis.Tick(3.0, false, NobodyElse);   // nothing new while a.cpp is outstanding

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

	analysis.Tick(1.0, false, NobodyElse);
	analysis.Tick(2.0, false, NobodyElse);

	CHECK(analysis.Apply(Colours(port.TicketOf("colours", "a.cpp"))));
	CHECK(analysis.Apply(Symbols(port.TicketOf("symbols", "a.cpp"))));

	// Retiring is a Tick's job, not an answer's: it needs the heldElsewhere predicate to know
	// whether anybody else already holds this document open with the server, and only a Tick is given
	// that predicate. The same tick asks for b.cpp's text.
	analysis.Tick(3.0, false, NobodyElse);

	CHECK(port.CountOf("close") == 1);

	const std::optional<CachedAnalysis> kept = cache.Take("a.cpp", OnDisk());

	REQUIRE(kept.has_value());
	CHECK_FALSE(kept->Semantic.IsEmpty());
	CHECK(kept->Declarations.size() == 1);

	analysis.Tick(4.0, false, NobodyElse);

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

	analysis.Tick(1.0, true, NobodyElse);
	analysis.Tick(2.0, true, NobodyElse);

	CHECK(port.Calls.empty());

	analysis.Tick(3.0, false, NobodyElse);
	analysis.Tick(4.0, false, NobodyElse);

	CHECK(port.CountOf("open") == 1);
}

TEST_CASE("A briefly busy document still holds everything back")
{
	FakePort port;
	FakeTexts texts;
	AnalysisCache cache{1024 * 1024};
	WorkspaceAnalysis analysis{port, texts, cache};

	analysis.Enqueue({"a.cpp"});

	// Nine seconds of unbroken busy is under the cap, and the pass waits it out.
	for (double now = 1.0; now <= 10.0; now += 1.0)
	{
		analysis.Tick(now, true, NobodyElse);
	}

	CHECK(port.Calls.empty());
}

TEST_CASE("A document busy for longer than anything real is stopped being waited for")
{
	FakePort port;
	FakeTexts texts;
	AnalysisCache cache{1024 * 1024};
	WorkspaceAnalysis analysis{port, texts, cache};

	analysis.Enqueue({"a.cpp"});

	// The foreground claims to be busy and never stops - a server configured but not installed,
	// whose symbol request retries forever. Past the cap the pass goes anyway.
	analysis.Tick(1.0, true, NobodyElse);

	CHECK(port.Calls.empty());

	analysis.Tick(1.0 + 11.0, true, NobodyElse);   // asks for a.cpp's text
	analysis.Tick(1.0 + 12.0, true, NobodyElse);   // the text arrives and a.cpp is opened

	CHECK(port.CountOf("open") == 1);
}

TEST_CASE("Busy frames broken by a quiet one do not add up towards the cap")
{
	FakePort port;
	FakeTexts texts;
	AnalysisCache cache{1024 * 1024};
	WorkspaceAnalysis analysis{port, texts, cache};

	// Busy for nine seconds, one quiet frame, then busy again for nine: nineteen seconds between
	// the first busy frame and the last, and not one unbroken run past the cap. Typing looks
	// exactly like this. The queue is filled after the quiet frame so that frame has nothing to
	// launch - what is under test is the clock, not what a quiet frame does with a queue.
	analysis.Tick(1.0, true, NobodyElse);
	analysis.Tick(10.0, true, NobodyElse);

	analysis.Tick(11.0, false, NobodyElse);

	analysis.Enqueue({"a.cpp"});

	analysis.Tick(12.0, true, NobodyElse);
	analysis.Tick(20.0, true, NobodyElse);

	CHECK(port.Calls.empty());
}

TEST_CASE("A flight already in the air is finished rather than abandoned when the editor gets busy")
{
	FakePort port;
	FakeTexts texts;
	AnalysisCache cache{1024 * 1024};
	WorkspaceAnalysis analysis{port, texts, cache};

	analysis.Enqueue({"a.cpp"});

	analysis.Tick(1.0, false, NobodyElse);
	analysis.Tick(2.0, false, NobodyElse);

	analysis.Tick(3.0, true, NobodyElse);

	CHECK(port.CountOf("close") == 0);

	CHECK(analysis.Apply(Colours(port.TicketOf("colours", "a.cpp"))));
	CHECK(analysis.Apply(Symbols(port.TicketOf("symbols", "a.cpp"))));

	// Still busy, and the flight still retires: what was already in the air is seen through.
	analysis.Tick(4.0, true, NobodyElse);

	CHECK(port.CountOf("close") == 1);
	CHECK(cache.Take("a.cpp", OnDisk()).has_value());
}

TEST_CASE("A file that somebody else already holds is never opened from here")
{
	FakePort port;
	FakeTexts texts;
	AnalysisCache cache{1024 * 1024};
	WorkspaceAnalysis analysis{port, texts, cache};

	analysis.Enqueue({"a.cpp"});

	analysis.Tick(1.0, false, SomebodyElse);
	analysis.Tick(2.0, false, SomebodyElse);

	CHECK(port.Calls.empty());
	CHECK(analysis.Progress().Done == 1);
}

TEST_CASE("A file somebody else holds is left alone, whether a tab or other holder")
{
	FakePort port;
	FakeTexts texts;
	AnalysisCache cache{1024 * 1024};
	WorkspaceAnalysis analysis{port, texts, cache};

	// A preview pane holding a.cpp sent the didOpen, and the predicate the application hands down
	// answers for holders, not for tabs. b.cpp is what the pass may have.
	const auto paneHoldsA = [](const std::filesystem::path& path)
	{ return path == std::filesystem::path{"a.cpp"}; };

	analysis.Enqueue({"a.cpp", "b.cpp"});

	analysis.Tick(1.0, false, paneHoldsA);
	analysis.Tick(2.0, false, paneHoldsA);

	CHECK(port.CountOf("open") == 1);
	CHECK(port.Calls.front().Path == std::filesystem::path{"b.cpp"});

	// And nothing was said about a.cpp at all: the skip happens before the read is asked for.
	for (const FakePort::Call& call : port.Calls)
	{
		CHECK(call.Path != std::filesystem::path{"a.cpp"});
	}
}

TEST_CASE("A file taken over while its text is being read is never opened")
{
	FakePort port;
	FakeTexts texts;
	AnalysisCache cache{1024 * 1024};
	WorkspaceAnalysis analysis{port, texts, cache};

	analysis.Enqueue({"a.cpp"});

	// Free at launch, so the read is asked for.
	analysis.Tick(1.0, false, NobodyElse);

	// Taken over by the time the text lands. Opening it now would overwrite what the server
	// remembers for the holder with the bytes on disk, which only shows up when the server dies
	// and its documents are replayed.
	analysis.Tick(2.0, false, SomebodyElse);

	CHECK(port.Calls.empty());

	// Counted, once: the queue must not wait on a file nobody is going to ask about.
	CHECK(analysis.Progress().Done == 1);

	analysis.Tick(3.0, false, SomebodyElse);

	CHECK(analysis.Progress().Done == 1);
}

TEST_CASE("A file that acquires a holder mid-flight is abandoned and nothing is cached")
{
	FakePort port;
	FakeTexts texts;
	AnalysisCache cache{1024 * 1024};
	WorkspaceAnalysis analysis{port, texts, cache};

	analysis.Enqueue({"a.cpp"});

	analysis.Tick(1.0, false, NobodyElse);
	analysis.Tick(2.0, false, NobodyElse);

	analysis.Apply(Colours(port.TicketOf("colours", "a.cpp")));
	analysis.Apply(Symbols(port.TicketOf("symbols", "a.cpp")));

	// Somebody else became the holder before the answers were retired.
	analysis.Tick(3.0, false, SomebodyElse);

	CHECK_FALSE(cache.Take("a.cpp", OnDisk()).has_value());

	// And not closed either: the holder owns the document now, CloseDocument counts no references,
	// and a holder only ever opens one once. A didClose from here would take the file away from it
	// for good.
	CHECK(port.CountOf("close") == 0);

	// Dropped all the same — the queue is not left waiting on a flight nobody will retire.
	CHECK(analysis.Progress().Done == 1);
}

TEST_CASE("A flight that times out after somebody else takes it over closes nothing")
{
	FakePort port;
	FakeTexts texts;
	AnalysisCache cache{1024 * 1024};
	WorkspaceAnalysis analysis{port, texts, cache};

	// One file, so the Done below is the timed-out flight's and nothing else's: a queue behind it
	// would be skipped in the same tick, every skip counting as done.
	analysis.Enqueue({"a.cpp"});

	analysis.Tick(1.0, false, NobodyElse);
	analysis.Tick(2.0, false, NobodyElse);

	// Never answered, and somebody else became a holder in the meantime.
	analysis.Tick(2.0 + 200.0, false, SomebodyElse);

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

	analysis.Tick(1.0, false, NobodyElse);
	analysis.Tick(2.0, false, NobodyElse);

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

	analysis.Tick(1.0, false, NobodyElse);
	analysis.Tick(2.0, false, NobodyElse);

	CHECK(analysis.Apply(Failed(port.TicketOf("colours", "a.cpp"))));
	CHECK(analysis.Apply(Failed(port.TicketOf("symbols", "a.cpp"))));

	analysis.Tick(3.0, false, NobodyElse);

	CHECK_FALSE(cache.Take("a.cpp", OnDisk()).has_value());

	analysis.Tick(4.0, false, NobodyElse);

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

	analysis.Tick(1.0, false, NobodyElse);
	analysis.Tick(2.0, false, NobodyElse);

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

	analysis.Tick(1.0, false, NobodyElse);
	analysis.Tick(2.0, false, NobodyElse);

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

	analysis.Tick(1.0, false, NobodyElse);
	analysis.Tick(2.0, false, NobodyElse);

	// Well past the timeout rather than exactly on it: a tick on the boundary would only be
	// asserting which way the comparison rounds.
	analysis.Tick(2.0 + 200.0, false, NobodyElse);

	CHECK(port.CountOf("close") == 1);

	analysis.Tick(2.0 + 201.0, false, NobodyElse);

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

	analysis.Tick(1.0, false, NobodyElse);
	analysis.Tick(2.0, false, NobodyElse);

	analysis.Tick(2.0 + 60.0, false, NobodyElse);

	CHECK(port.CountOf("close") == 0);

	CHECK(analysis.Apply(Colours(port.TicketOf("colours", "a.cpp"))));
	CHECK(analysis.Apply(Symbols(port.TicketOf("symbols", "a.cpp"))));

	analysis.Tick(2.0 + 61.0, false, NobodyElse);

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

	analysis.Tick(1.0, false, NobodyElse);
	analysis.Tick(2.0, false, NobodyElse);

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
	analysis.Tick(1.0, false, NobodyElse);
	analysis.Tick(2.0, false, NobodyElse);
	analysis.Apply(Colours(port.TicketOf("colours", "a.cpp")));
	analysis.Apply(Symbols(port.TicketOf("symbols", "a.cpp")));
	analysis.Tick(3.0, false, NobodyElse);

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

	analysis.Tick(1.0, false, NobodyElse);
	analysis.Tick(2.0, false, NobodyElse);

	analysis.Clear();

	CHECK(port.CountOf("close") == 1);
	CHECK(analysis.Progress().Total == 0);

	analysis.Tick(3.0, false, NobodyElse);
	analysis.Tick(4.0, false, NobodyElse);

	CHECK(port.CountOf("open") == 1);
}

TEST_CASE("Closing the folder while somebody else holds the file in flight closes nothing")
{
	FakePort port;
	FakeTexts texts;
	AnalysisCache cache{1024 * 1024};
	WorkspaceAnalysis analysis{port, texts, cache};

	analysis.Enqueue({"a.cpp"});

	analysis.Tick(1.0, false, NobodyElse);
	analysis.Tick(2.0, false, NobodyElse);

	// The tick that tells the flight somebody else now holds the file. Clear has no predicate of its
	// own — the folder can close from a settings toggle — so this is the only answer it will ever have.
	analysis.Tick(3.0, false, SomebodyElse);

	analysis.Clear();

	CHECK(port.CountOf("close") == 0);
	CHECK(analysis.Progress().Total == 0);
}

TEST_CASE("Destruction closes what was opened here, and nothing somebody else holds")
{
	FakePort port;
	FakeTexts texts;
	AnalysisCache cache{1024 * 1024};

	SUBCASE("nothing holds the file, so it is closed")
	{
		{
			WorkspaceAnalysis analysis{port, texts, cache};

			analysis.Enqueue({"a.cpp"});

			analysis.Tick(1.0, false, NobodyElse);
			analysis.Tick(2.0, false, NobodyElse);
		}

		CHECK(port.CountOf("close") == 1);
	}

	SUBCASE("somebody else holds the file, so it is left open for them")
	{
		{
			WorkspaceAnalysis analysis{port, texts, cache};

			analysis.Enqueue({"a.cpp"});

			analysis.Tick(1.0, false, NobodyElse);
			analysis.Tick(2.0, false, NobodyElse);
			analysis.Tick(3.0, false, SomebodyElse);
		}

		CHECK(port.CountOf("close") == 0);
	}
}
