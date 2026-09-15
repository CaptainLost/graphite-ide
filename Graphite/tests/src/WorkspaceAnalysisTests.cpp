#include <doctest/doctest.h>

#include "Lang/Analysis/WorkspaceAnalysis.h"

#include "Lang/Analysis/AnalysisStore.h"

#include "Editor/AnalysisCache.h"

#include "TempDirectory.h"

#include <chrono>
#include <cstdint>
#include <string>
#include <thread>
#include <vector>

using namespace Graphite;

namespace {

	// Records what it was told instead of talking to a process. Every claim this feature makes
	// about a server — no more than the limit open at once, never two analyses of one path, none
	// for a file somebody else already holds, one didClose for every didOpen — is a claim about
	// this log.
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

	// Answers every request it is given, in the order it was given them, and out of the same
	// frame, so a test can drive several analyses at once without a thread or a wait.
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

	// Drives SetRoot's listing to completion the way the application does — by polling. Bounded
	// rather than open-ended: a hang here should fail the case, not the suite.
	void WaitUntilReady(AnalysisStore& store)
	{
		for (int attempt = 0; attempt < 200 && !store.IsReady(); ++attempt)
		{
			store.Poll();
			std::this_thread::sleep_for(std::chrono::milliseconds(10));
		}

		REQUIRE(store.IsReady());
	}

	// The store encodes and writes on a worker, so a test that asserts on the disk has to wait.
	void WaitForWrites(AnalysisStore& store)
	{
		for (int attempt = 0; attempt < 200 && !store.IsQuiet(); ++attempt)
		{
			std::this_thread::sleep_for(std::chrono::milliseconds(10));
		}

		REQUIRE(store.IsQuiet());
	}
}

TEST_CASE("One file is opened at a time, however many are queued")
{
	FakePort port;
	FakeTexts texts;
	AnalysisCache cache{1024 * 1024};
	WorkspaceAnalysis analysis{port, texts, cache, nullptr};

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
	WorkspaceAnalysis analysis{port, texts, cache, nullptr};

	analysis.Enqueue({"a.cpp", "b.cpp"});

	analysis.Tick(1.0, false, NobodyElse);
	analysis.Tick(2.0, false, NobodyElse);

	CHECK(analysis.Apply(Colours(port.TicketOf("colours", "a.cpp"))));
	CHECK(analysis.Apply(Symbols(port.TicketOf("symbols", "a.cpp"))));

	// Finishing is a Tick's job, not an answer's: it needs the heldElsewhere predicate to know
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
	WorkspaceAnalysis analysis{port, texts, cache, nullptr};

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
	WorkspaceAnalysis analysis{port, texts, cache, nullptr};

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
	WorkspaceAnalysis analysis{port, texts, cache, nullptr};

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
	WorkspaceAnalysis analysis{port, texts, cache, nullptr};

	// Busy for nine seconds, one quiet frame, then busy again for nine: nineteen seconds between
	// the first busy frame and the last, and not one unbroken run past the cap. Typing looks
	// exactly like this. The queue is filled after the quiet frame so that frame has nothing to
	// start - what is under test is the clock, not what a quiet frame does with a queue.
	analysis.Tick(1.0, true, NobodyElse);
	analysis.Tick(10.0, true, NobodyElse);

	analysis.Tick(11.0, false, NobodyElse);

	analysis.Enqueue({"a.cpp"});

	analysis.Tick(12.0, true, NobodyElse);
	analysis.Tick(20.0, true, NobodyElse);

	CHECK(port.Calls.empty());
}

TEST_CASE("An analysis already outstanding is finished rather than abandoned when the editor gets "
		  "busy")
{
	FakePort port;
	FakeTexts texts;
	AnalysisCache cache{1024 * 1024};
	WorkspaceAnalysis analysis{port, texts, cache, nullptr};

	analysis.Enqueue({"a.cpp"});

	analysis.Tick(1.0, false, NobodyElse);
	analysis.Tick(2.0, false, NobodyElse);

	analysis.Tick(3.0, true, NobodyElse);

	CHECK(port.CountOf("close") == 0);

	CHECK(analysis.Apply(Colours(port.TicketOf("colours", "a.cpp"))));
	CHECK(analysis.Apply(Symbols(port.TicketOf("symbols", "a.cpp"))));

	// Still busy, and the analysis still finishes: what was already outstanding is seen through.
	analysis.Tick(4.0, true, NobodyElse);

	CHECK(port.CountOf("close") == 1);
	CHECK(cache.Take("a.cpp", OnDisk()).has_value());
}

TEST_CASE("A file that somebody else already holds is never opened from here")
{
	FakePort port;
	FakeTexts texts;
	AnalysisCache cache{1024 * 1024};
	WorkspaceAnalysis analysis{port, texts, cache, nullptr};

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
	WorkspaceAnalysis analysis{port, texts, cache, nullptr};

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
	WorkspaceAnalysis analysis{port, texts, cache, nullptr};

	analysis.Enqueue({"a.cpp"});

	// Free when it was taken off the queue, so the read is asked for.
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

TEST_CASE("A file that acquires a holder while it is outstanding is dropped and nothing is cached")
{
	FakePort port;
	FakeTexts texts;
	AnalysisCache cache{1024 * 1024};
	WorkspaceAnalysis analysis{port, texts, cache, nullptr};

	analysis.Enqueue({"a.cpp"});

	analysis.Tick(1.0, false, NobodyElse);
	analysis.Tick(2.0, false, NobodyElse);

	analysis.Apply(Colours(port.TicketOf("colours", "a.cpp")));
	analysis.Apply(Symbols(port.TicketOf("symbols", "a.cpp")));

	// Somebody else became the holder before the answers were finished with.
	analysis.Tick(3.0, false, SomebodyElse);

	CHECK_FALSE(cache.Take("a.cpp", OnDisk()).has_value());

	// And not closed either: the holder owns the document now, CloseDocument counts no references,
	// and a holder only ever opens one once. A didClose from here would take the file away from it
	// for good.
	CHECK(port.CountOf("close") == 0);

	// Dropped all the same — the queue is not left waiting on an analysis nobody will finish.
	CHECK(analysis.Progress().Done == 1);
}

TEST_CASE("An analysis that times out after somebody else takes it over closes nothing")
{
	FakePort port;
	FakeTexts texts;
	AnalysisCache cache{1024 * 1024};
	WorkspaceAnalysis analysis{port, texts, cache, nullptr};

	// One file, so the Done below is the timed-out analysis's and nothing else's: a queue behind it
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
	WorkspaceAnalysis analysis{port, texts, cache, nullptr};

	// Refused with nothing outstanding, and refused again with an analysis outstanding — the second
	// is what makes this a claim about matching tickets rather than about being idle.
	CHECK_FALSE(analysis.Apply(Colours(static_cast<LanguageServices::Ticket>(9999))));

	analysis.Enqueue({"a.cpp"});

	analysis.Tick(1.0, false, NobodyElse);
	analysis.Tick(2.0, false, NobodyElse);

	CHECK_FALSE(analysis.Apply(Colours(static_cast<LanguageServices::Ticket>(9999))));
	CHECK(analysis.Apply(Colours(port.TicketOf("colours", "a.cpp"))));
}

TEST_CASE("A failed request finishes the file without caching it, and the queue moves on")
{
	FakePort port;
	FakeTexts texts;
	AnalysisCache cache{1024 * 1024};
	WorkspaceAnalysis analysis{port, texts, cache, nullptr};

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
	WorkspaceAnalysis analysis{port, texts, cache, nullptr};

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
	WorkspaceAnalysis analysis{port, texts, cache, nullptr};

	analysis.Enqueue({"notes.txt"});

	analysis.Tick(1.0, false, NobodyElse);
	analysis.Tick(2.0, false, NobodyElse);

	CHECK(port.Calls.empty());
	CHECK(analysis.Progress().Done == 1);
}

TEST_CASE("An analysis that is never answered is given up on rather than blocking the queue "
		  "forever")
{
	FakePort port;
	FakeTexts texts;
	AnalysisCache cache{1024 * 1024};
	WorkspaceAnalysis analysis{port, texts, cache, nullptr};

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

TEST_CASE("An analysis answered inside the timeout is not given up on")
{
	FakePort port;
	FakeTexts texts;
	AnalysisCache cache{1024 * 1024};
	WorkspaceAnalysis analysis{port, texts, cache, nullptr};

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
	WorkspaceAnalysis analysis{port, texts, cache, nullptr};

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
	WorkspaceAnalysis analysis{port, texts, cache, nullptr};

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
	WorkspaceAnalysis analysis{port, texts, cache, nullptr};

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

TEST_CASE("Closing the folder while somebody else holds an outstanding file closes nothing")
{
	FakePort port;
	FakeTexts texts;
	AnalysisCache cache{1024 * 1024};
	WorkspaceAnalysis analysis{port, texts, cache, nullptr};

	analysis.Enqueue({"a.cpp"});

	analysis.Tick(1.0, false, NobodyElse);
	analysis.Tick(2.0, false, NobodyElse);

	// The tick that tells the pending analysis somebody else now holds the file. Clear has no
	// predicate of its own — the folder can close from a settings toggle — so this is the only
	// answer it will ever have.
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
			WorkspaceAnalysis analysis{port, texts, cache, nullptr};

			analysis.Enqueue({"a.cpp"});

			analysis.Tick(1.0, false, NobodyElse);
			analysis.Tick(2.0, false, NobodyElse);
		}

		CHECK(port.CountOf("close") == 1);
	}

	SUBCASE("somebody else holds the file, so it is left open for them")
	{
		{
			WorkspaceAnalysis analysis{port, texts, cache, nullptr};

			analysis.Enqueue({"a.cpp"});

			analysis.Tick(1.0, false, NobodyElse);
			analysis.Tick(2.0, false, NobodyElse);
			analysis.Tick(3.0, false, SomebodyElse);
		}

		CHECK(port.CountOf("close") == 0);
	}
}

TEST_CASE("Up to the limit are outstanding at once, and no more")
{
	FakePort port;
	FakeTexts texts;
	AnalysisCache cache{1024 * 1024};
	WorkspaceAnalysis analysis{port, texts, cache, nullptr};

	analysis.SetConcurrency(3);
	analysis.Enqueue({"a.cpp", "b.cpp", "c.cpp", "d.cpp", "e.cpp"});

	analysis.Tick(1.0, false, NobodyElse);   // three reads requested
	analysis.Tick(2.0, false, NobodyElse);   // their texts arrive and three are opened

	CHECK(port.CountOf("open") == 3);

	analysis.Tick(3.0, false, NobodyElse);

	CHECK(port.CountOf("open") == 3);
}

TEST_CASE("A limit of one behaves exactly as it did before there was a limit")
{
	FakePort port;
	FakeTexts texts;
	AnalysisCache cache{1024 * 1024};
	WorkspaceAnalysis analysis{port, texts, cache, nullptr};

	analysis.SetConcurrency(1);
	analysis.Enqueue({"a.cpp", "b.cpp"});

	analysis.Tick(1.0, false, NobodyElse);
	analysis.Tick(2.0, false, NobodyElse);
	analysis.Tick(3.0, false, NobodyElse);

	CHECK(port.CountOf("open") == 1);
}

TEST_CASE("A limit of zero is refused rather than stopping the pass")
{
	FakePort port;
	FakeTexts texts;
	AnalysisCache cache{1024 * 1024};
	WorkspaceAnalysis analysis{port, texts, cache, nullptr};

	// Zero is what a feature switched off looks like, and SetEnabled is what says that. A limit
	// of zero here would be a pass that never starts anything and never reports why.
	analysis.SetConcurrency(0);
	analysis.Enqueue({"a.cpp"});

	analysis.Tick(1.0, false, NobodyElse);
	analysis.Tick(2.0, false, NobodyElse);

	CHECK(port.CountOf("open") == 1);
}

TEST_CASE("One path is never outstanding twice, however it reaches the queue")
{
	FakePort port;
	FakeTexts texts;
	AnalysisCache cache{1024 * 1024};
	WorkspaceAnalysis analysis{port, texts, cache, nullptr};

	analysis.SetConcurrency(4);

	// The same path twice. Two openers of one document means the first close takes it away from
	// the second — see ADR 0512.
	analysis.Enqueue({"a.cpp", "a.cpp"});

	analysis.Tick(1.0, false, NobodyElse);
	analysis.Tick(2.0, false, NobodyElse);

	CHECK(port.CountOf("open") == 1);

	// The duplicate was counted against the total it was added to, so the bar still reaches its
	// end: one file analysed, one skipped as already outstanding, two queued.
	CHECK(analysis.Progress().Total == 2);

	CHECK(analysis.Apply(Colours(port.TicketOf("colours", "a.cpp"))));
	CHECK(analysis.Apply(Symbols(port.TicketOf("symbols", "a.cpp"))));

	analysis.Tick(3.0, false, NobodyElse);

	CHECK(port.CountOf("close") == 1);
	CHECK(analysis.Progress().Done == 2);
}

TEST_CASE("Finishing one of several starts the next, and closes only the one that finished")
{
	FakePort port;
	FakeTexts texts;
	AnalysisCache cache{1024 * 1024};
	WorkspaceAnalysis analysis{port, texts, cache, nullptr};

	analysis.SetConcurrency(2);
	analysis.Enqueue({"a.cpp", "b.cpp", "c.cpp"});

	analysis.Tick(1.0, false, NobodyElse);
	analysis.Tick(2.0, false, NobodyElse);

	REQUIRE(port.CountOf("open") == 2);

	CHECK(analysis.Apply(Colours(port.TicketOf("colours", "a.cpp"))));
	CHECK(analysis.Apply(Symbols(port.TicketOf("symbols", "a.cpp"))));

	analysis.Tick(3.0, false, NobodyElse);

	CHECK(port.CountOf("close") == 1);
	CHECK(port.Calls.back().Path == std::filesystem::path{"a.cpp"});
	CHECK(analysis.Progress().Done == 1);

	analysis.Tick(4.0, false, NobodyElse);

	CHECK(port.CountOf("open") == 3);
}

TEST_CASE("The yield holds back every slot, not just the next one")
{
	FakePort port;
	FakeTexts texts;
	AnalysisCache cache{1024 * 1024};
	WorkspaceAnalysis analysis{port, texts, cache, nullptr};

	analysis.SetConcurrency(3);
	analysis.Enqueue({"a.cpp", "b.cpp", "c.cpp"});

	analysis.Tick(1.0, true, NobodyElse);
	analysis.Tick(2.0, true, NobodyElse);

	CHECK(port.Calls.empty());
}

TEST_CASE("Progress counts each file exactly once with several outstanding")
{
	FakePort port;
	FakeTexts texts;
	AnalysisCache cache{1024 * 1024};
	WorkspaceAnalysis analysis{port, texts, cache, nullptr};

	analysis.SetConcurrency(3);
	analysis.Enqueue({"a.cpp", "b.cpp", "c.cpp"});

	analysis.Tick(1.0, false, NobodyElse);
	analysis.Tick(2.0, false, NobodyElse);

	for (const char* name : {"a.cpp", "b.cpp", "c.cpp"})
	{
		analysis.Apply(Colours(port.TicketOf("colours", name)));
		analysis.Apply(Symbols(port.TicketOf("symbols", name)));
	}

	analysis.Tick(3.0, false, NobodyElse);

	CHECK(analysis.Progress().Done == 3);
	CHECK(analysis.Progress().Total == 3);
}

TEST_CASE("Lowering the limit starts nothing new and lets the outstanding ones finish")
{
	FakePort port;
	FakeTexts texts;
	AnalysisCache cache{1024 * 1024};
	WorkspaceAnalysis analysis{port, texts, cache, nullptr};

	analysis.SetConcurrency(3);
	analysis.Enqueue({"a.cpp", "b.cpp", "c.cpp", "d.cpp"});

	analysis.Tick(1.0, false, NobodyElse);
	analysis.Tick(2.0, false, NobodyElse);

	REQUIRE(port.CountOf("open") == 3);

	analysis.SetConcurrency(1);
	analysis.Tick(3.0, false, NobodyElse);

	// Nothing new, and nothing torn down: what is outstanding was already paid for.
	CHECK(port.CountOf("open") == 3);
	CHECK(port.CountOf("close") == 0);
}

TEST_CASE("One timing out does not disturb the others")
{
	FakePort port;
	FakeTexts texts;
	AnalysisCache cache{1024 * 1024};
	WorkspaceAnalysis analysis{port, texts, cache, nullptr};

	analysis.SetConcurrency(2);
	analysis.Enqueue({"a.cpp", "b.cpp"});

	analysis.Tick(1.0, false, NobodyElse);
	analysis.Tick(2.0, false, NobodyElse);

	REQUIRE(port.CountOf("open") == 2);

	CHECK(analysis.Apply(Colours(port.TicketOf("colours", "b.cpp"))));
	CHECK(analysis.Apply(Symbols(port.TicketOf("symbols", "b.cpp"))));

	// b.cpp is answered and finished with; a.cpp is never answered and times out later.
	analysis.Tick(3.0, false, NobodyElse);

	CHECK(port.CountOf("close") == 1);

	analysis.Tick(2.0 + 200.0, false, NobodyElse);

	CHECK(port.CountOf("close") == 2);
	CHECK(analysis.Progress().Done == 2);
}

TEST_CASE("A file the store already has is skipped rather than asked about again")
{
	TempDirectory temp;
	const std::filesystem::path source = temp.MakeFile("a.cpp", "int x;");

	AnalysisStore store;
	store.SetRoot(temp.GetPath());
	WaitUntilReady(store);

	CachedAnalysis kept;
	Declaration declaration;
	declaration.Name = "Draw";
	kept.Declarations.push_back(std::move(declaration));
	kept.ModifiedAt = std::filesystem::last_write_time(source);

	store.Write(source, kept);

	FakePort port;
	FakeTexts texts;
	AnalysisCache cache{1024 * 1024};
	WorkspaceAnalysis analysis{port, texts, cache, &store};

	analysis.Enqueue({source});
	analysis.Tick(1.0, false, NobodyElse);
	analysis.Tick(2.0, false, NobodyElse);

	CHECK(port.Calls.empty());
	CHECK(analysis.Progress().Done == 1);
}

TEST_CASE("A completed analysis reaches the store as well as the memory cache")
{
	TempDirectory temp;
	const std::filesystem::path source = temp.MakeFile("a.cpp", "int x;");

	AnalysisStore store;
	store.SetRoot(temp.GetPath());
	WaitUntilReady(store);

	FakePort port;
	FakeTexts texts;
	AnalysisCache cache{1024 * 1024};
	WorkspaceAnalysis analysis{port, texts, cache, &store};

	analysis.Enqueue({source});

	analysis.Tick(1.0, false, NobodyElse);
	analysis.Tick(2.0, false, NobodyElse);

	CHECK(analysis.Apply(Colours(port.TicketOf("colours", source))));
	CHECK(analysis.Apply(Symbols(port.TicketOf("symbols", source))));

	analysis.Tick(3.0, false, NobodyElse);

	// Both tiers, not one: without the store write nothing would ever reach the disk except
	// through a closing tab, and a second launch would redo the whole workspace.
	CHECK(cache.Take(source, OnDisk()).has_value());

	CHECK(store.Has(source));

	WaitForWrites(store);

	CHECK(store.Read(source, OnDisk()).has_value());
}
