#include <doctest/doctest.h>

#include "Sessions/LiveTranscript.h"
#include "Sessions/RequestTimeline.h"

#include "TempDirectory.h"
#include "TranscriptFixtures.h"

#include <chrono>
#include <fstream>
#include <memory>
#include <string>
#include <thread>

using namespace Graphite;

namespace {
	void Append(const std::filesystem::path& file, const std::string& bytes)
	{
		std::ofstream stream{file, std::ios::binary | std::ios::app};
		stream << bytes;
	}

	// Takes until the session has `turns` turns, or fails after five seconds.
	void WaitForTurns(LiveTranscript& live, Session& session, std::size_t turns)
	{
		const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{5};

		while (session.GetTurns().size() != turns)
		{
			live.Take(session);
			REQUIRE(std::chrono::steady_clock::now() < deadline);
			std::this_thread::sleep_for(std::chrono::milliseconds{5});
		}
	}
}

TEST_CASE("A live transcript reads what is there, then only what is appended")
{
	TempDirectory temp;
	const std::filesystem::path file = temp.MakeFile("s.jsonl", Fixtures::UserPrompt("one"));

	LiveTranscript live{file};
	Session session;

	live.Poll();
	WaitForTurns(live, session, 1);

	Append(file, Fixtures::UserPrompt("two", "2026-09-17T09:01:00.000Z"));
	live.Poll();
	WaitForTurns(live, session, 2);

	CHECK(session.GetTurns()[1].Prompt == "two");
}

TEST_CASE("The timeline follows the transcript and restarts with it")
{
	TempDirectory temp;
	const std::filesystem::path file = temp.MakeFile("s.jsonl",
		Fixtures::UserPrompt("one") +
		Fixtures::Assistant("r1", Fixtures::TextBlock("a"), Fixtures::Usage(1, 1)) +
		Fixtures::Assistant("r2", Fixtures::TextBlock("b"), Fixtures::Usage(1, 1), "claude-opus-5",
							"2026-09-17T09:00:02.000Z"));

	LiveTranscript live{file};
	Session session;
	RequestTimeline timeline;

	live.Poll();

	{
		const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{5};
		while (timeline.GetRequests().size() != 2)
		{
			live.Take(session, timeline);
			REQUIRE(std::chrono::steady_clock::now() < deadline);
			std::this_thread::sleep_for(std::chrono::milliseconds{5});
		}
	}

	const std::uint64_t versionBefore = timeline.GetVersion();

	{
		std::ofstream stream{file, std::ios::binary | std::ios::trunc};
		stream << Fixtures::UserPrompt("one") + Fixtures::Assistant("r1", Fixtures::TextBlock("a"), Fixtures::Usage(1, 1));
	}

	live.Poll();

	{
		const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{5};
		while (timeline.GetRequests().size() != 1)
		{
			live.Take(session, timeline);
			REQUIRE(std::chrono::steady_clock::now() < deadline);
			std::this_thread::sleep_for(std::chrono::milliseconds{5});
		}
	}

	// A restart moves the timeline's version too, distinct from ordinary growth.
	CHECK(timeline.GetVersion() != versionBefore);
}

TEST_CASE("A transcript that shrank is read again from nothing")
{
	TempDirectory temp;
	const std::filesystem::path file =
		temp.MakeFile("s.jsonl", Fixtures::UserPrompt("one") + Fixtures::UserPrompt("two", "2026-09-17T09:01:00.000Z"));

	LiveTranscript live{file};
	Session session;
	live.Poll();
	WaitForTurns(live, session, 2);

	const std::uint64_t generationBefore = session.GetGeneration();

	{
		std::ofstream stream{file, std::ios::binary | std::ios::trunc};
		stream << Fixtures::UserPrompt("only");
	}

	live.Poll();
	WaitForTurns(live, session, 1);

	CHECK(session.GetTurns()[0].Prompt == "only");

	// A generation of its own, not just a version that happened to go backwards: a view
	// building rows off the version alone cannot tell this apart from ordinary growth.
	CHECK(session.GetGeneration() != generationBefore);
}

TEST_CASE("A transcript that does not exist yet is read once it appears")
{
	TempDirectory temp;
	const std::filesystem::path file = temp.GetPath() / "later.jsonl";

	LiveTranscript live{file};
	Session session;

	live.Poll();
	std::this_thread::sleep_for(std::chrono::milliseconds{50});
	CHECK_FALSE(live.Take(session));

	temp.MakeFile("later.jsonl", Fixtures::UserPrompt("hello"));
	live.Poll();
	WaitForTurns(live, session, 1);
}

TEST_CASE("Twenty thousand lines arrive complete and once, however the polls interleave")
{
	TempDirectory temp;
	const std::filesystem::path file = temp.MakeFile("s.jsonl", "");

	LiveTranscript live{file};
	Session session;

	constexpr std::size_t kTurns = 20'000;

	for (std::size_t index = 0; index < kTurns; ++index)
	{
		Append(file, Fixtures::UserPrompt("p" + std::to_string(index)));

		// Polls while a read is running must not start a second read or drop the request.
		if (index % 1000 == 0)
			live.Poll();
	}

	live.Poll();
	WaitForTurns(live, session, kTurns);

	CHECK(session.GetTurns().front().Prompt == "p0");
	CHECK(session.GetTurns().back().Prompt == "p19999");
}

TEST_CASE("Subagent usage arrives with the transcript and again when a subagent writes")
{
	TempDirectory temp;
	const std::filesystem::path file = temp.MakeFile("p/s.jsonl", Fixtures::UserPrompt("one"));

	LiveTranscript live{file};
	Session session;
	SubagentUsage subagents;

	live.Poll();
	WaitForTurns(live, session, 1);

	temp.MakeFile("p/s/subagents/agent-a1.jsonl", Fixtures::Assistant("r", Fixtures::TextBlock("y"), Fixtures::Usage(5, 6)));
	live.Poll();

	const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{5};
	while (!subagents.contains("a1"))
	{
		live.TakeSubagents(subagents);
		REQUIRE(std::chrono::steady_clock::now() < deadline);
		std::this_thread::sleep_for(std::chrono::milliseconds{5});
	}

	CHECK(subagents.at("a1") == UsageByModel{{{"claude-opus-5", false}, {5, 6, 0, 0, 0}}});
}

namespace {
	// Takes the subagents' usage until agent `id` has `expected`, or fails after five seconds.
	void WaitForSubagent(LiveTranscript& live, SubagentUsage& subagents, const std::string& id,
						 const UsageByModel& expected)
	{
		const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{5};

		while (!subagents.contains(id) || subagents.at(id) != expected)
		{
			live.TakeSubagents(subagents);
			REQUIRE(std::chrono::steady_clock::now() < deadline);
			std::this_thread::sleep_for(std::chrono::milliseconds{5});
		}
	}
}

TEST_CASE("A subagent's transcript is followed from where the last read stopped")
{
	TempDirectory temp;
	const std::filesystem::path file = temp.MakeFile("p/s.jsonl", Fixtures::UserPrompt("one"));
	const std::string first = Fixtures::Assistant("r1", Fixtures::TextBlock("y"), Fixtures::Usage(5, 6));
	const std::filesystem::path agent = temp.MakeFile("p/s/subagents/agent-a1.jsonl", first);

	LiveTranscript live{file};
	Session session;
	SubagentUsage subagents;

	live.Poll();
	WaitForSubagent(live, subagents, "a1", {{{"claude-opus-5", false}, {5, 6, 0, 0, 0}}});

	Append(agent, Fixtures::Assistant("r2", Fixtures::TextBlock("z"), Fixtures::Usage(1, 2)));
	live.Poll();
	WaitForSubagent(live, subagents, "a1", {{{"claude-opus-5", false}, {6, 8, 0, 0, 0}}});

	// The first line blanked in place, the file no shorter: a read from the start would lose
	// its usage, a read from where the last one stopped keeps it.
	{
		std::fstream stream{agent, std::ios::binary | std::ios::in | std::ios::out};
		stream.seekp(0);
		stream << std::string(first.size() - 1, ' ');
	}

	Append(agent, Fixtures::Assistant("r3", Fixtures::TextBlock("w"), Fixtures::Usage(1, 1)));
	live.Poll();
	WaitForSubagent(live, subagents, "a1", {{{"claude-opus-5", false}, {7, 9, 0, 0, 0}}});
}

namespace {
	// Polls GetProgress until `done` holds, or fails after `seconds`.
	template <typename Done>
	LiveTranscript::LoadProgress WaitForProgress(const LiveTranscript& live, Done done, int seconds = 60)
	{
		const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{seconds};

		while (true)
		{
			const LiveTranscript::LoadProgress progress = live.GetProgress();
			if (done(progress))
				return progress;

			REQUIRE(std::chrono::steady_clock::now() < deadline);
			std::this_thread::sleep_for(std::chrono::milliseconds{1});
		}
	}

	bool AllRead(const LiveTranscript::LoadProgress& progress)
	{
		return progress.FirstPassDone && progress.SubagentsWalked;
	}

	// A transcript of `lines` long prompts, written in one go.
	std::string LongTranscript(std::size_t lines)
	{
		std::string bytes;
		const std::string filler(300, 'x');

		for (std::size_t index = 0; index < lines; ++index)
			bytes += Fixtures::UserPrompt(filler + std::to_string(index));

		return bytes;
	}

	// A subagent transcript of `lines` requests.
	std::string SubagentTranscript(std::size_t lines)
	{
		std::string bytes;
		const std::string filler(300, 'y');

		for (std::size_t index = 0; index < lines; ++index)
			bytes += Fixtures::Assistant("r" + std::to_string(index), Fixtures::TextBlock(filler), Fixtures::Usage(1, 1));

		return bytes;
	}
}

TEST_CASE("Loading reports the bytes read of the transcript and the subagents read, then that the first pass is done")
{
	TempDirectory temp;
	const std::string bytes = Fixtures::UserPrompt("one") + Fixtures::UserPrompt("two", "2026-09-17T09:01:00.000Z");
	const std::filesystem::path file = temp.MakeFile("p/s.jsonl", bytes);
	temp.MakeFile("p/s/subagents/agent-a1.jsonl", SubagentTranscript(2));
	temp.MakeFile("p/s/subagents/agent-a2.jsonl", SubagentTranscript(3));

	LiveTranscript live{file};

	CHECK_FALSE(live.GetProgress().FirstPassDone);

	live.Poll();
	const LiveTranscript::LoadProgress progress = WaitForProgress(live, AllRead);

	CHECK(progress.Bytes == bytes.size());
	CHECK(progress.Total == bytes.size());
	CHECK(progress.SubagentsTotal == 2);
	CHECK(progress.SubagentsDone == 2);
}

TEST_CASE("A transcript larger than one chunk arrives in pieces, complete and in order")
{
	TempDirectory temp;
	constexpr std::size_t kLines = 8'000;
	const std::filesystem::path file = temp.MakeFile("s.jsonl", LongTranscript(kLines));

	LiveTranscript live{file};
	Session session;

	live.Poll();
	WaitForTurns(live, session, kLines);

	CHECK(session.GetTurns().front().Prompt == std::string(300, 'x') + "0");
	CHECK(session.GetTurns().back().Prompt == std::string(300, 'x') + std::to_string(kLines - 1));
}

TEST_CASE("A missing transcript still ends its first pass, so the view does not wait on it forever")
{
	TempDirectory temp;
	LiveTranscript live{temp.GetPath() / "none.jsonl"};

	live.Poll();
	const LiveTranscript::LoadProgress progress =
		WaitForProgress(live, [](const LiveTranscript::LoadProgress& p) { return p.FirstPassDone; }, 5);

	CHECK(progress.Total == 0);
}

namespace {
	using Clock = std::chrono::steady_clock;

	// A session large enough that reading it takes a while in any build: a transcript of a
	// few chunks and a subagent folder of many files. Built once for the tests that close a
	// reader mid-read, with the time a whole read of it takes measured once.
	struct LargeSession
	{
		TempDirectory Temp;
		std::filesystem::path File;
		Clock::duration FullRead{};

		LargeSession()
		{
			File = Temp.MakeFile("p/s.jsonl", LongTranscript(20'000));

			const std::string subagent = SubagentTranscript(150);
			for (int index = 0; index < 80; ++index)
				Temp.MakeFile("p/s/subagents/agent-a" + std::to_string(index) + ".jsonl", subagent);

			LiveTranscript live{File};
			const Clock::time_point start = Clock::now();
			live.Poll();
			WaitForProgress(live, AllRead, 300);
			FullRead = Clock::now() - start;
		}
	};

	const LargeSession& GetLargeSession()
	{
		static const LargeSession session;
		return session;
	}

	// Destroys `live` and says how long that took.
	Clock::duration TimeDestroy(std::unique_ptr<LiveTranscript>& live)
	{
		const Clock::time_point start = Clock::now();
		live.reset();
		return Clock::now() - start;
	}
}

// The bound is relative, a quarter of the time the same files take to read in full in this
// very run, so it holds in Debug and Release and on a slow machine alike: destroying stops
// after the chunk or the subagent file in hand, which is a small part of the whole.
TEST_CASE("Closing a transcript while its main file is read returns without finishing the read")
{
	const LargeSession& large = GetLargeSession();

	auto live = std::make_unique<LiveTranscript>(large.File);
	live->Poll();
	WaitForProgress(*live, [](const LiveTranscript::LoadProgress& p) { return p.Bytes > 0; });

	const LiveTranscript::LoadProgress before = live->GetProgress();
	const Clock::duration destroy = TimeDestroy(live);

	CHECK_FALSE(before.FirstPassDone);
	CHECK(destroy < large.FullRead / 4);
}

TEST_CASE("Closing a transcript while its subagents are read returns without finishing the walk")
{
	const LargeSession& large = GetLargeSession();

	auto live = std::make_unique<LiveTranscript>(large.File);
	live->Poll();
	WaitForProgress(*live, [](const LiveTranscript::LoadProgress& p)
					{ return p.FirstPassDone && p.SubagentsTotal > 0 && !p.SubagentsWalked; }, 300);

	const LiveTranscript::LoadProgress before = live->GetProgress();
	const Clock::duration destroy = TimeDestroy(live);

	CHECK(before.SubagentsDone < before.SubagentsTotal);
	CHECK(destroy < large.FullRead / 4);
}
