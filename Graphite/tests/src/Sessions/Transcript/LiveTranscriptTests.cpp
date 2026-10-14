#include <doctest/doctest.h>

#include "Sessions/Transcript/LiveTranscript.h"
#include "Sessions/Requests/RequestTimeline.h"
#include "Sessions/Transcript/SubagentLinks.h"

#include "Core/Base/Timestep.h"

#include "TempDirectory.h"
#include "TranscriptFixtures.h"

#include <chrono>
#include <fstream>
#include <functional>
#include <memory>
#include <string>
#include <thread>

using namespace Graphite;

namespace {
	using Clock = Core::SteadyClock;

	void Append(const std::filesystem::path& file, const std::string& bytes)
	{
		std::ofstream stream{file, std::ios::binary | std::ios::app};
		stream << bytes;
	}

	void WaitForTurns(LiveTranscript& live, Session& session, std::size_t turns)
	{
		const Clock::time_point deadline = Clock::now() + std::chrono::seconds{5};

		while (session.GetTurns().size() != turns && Clock::now() < deadline)
		{
			live.Take(session);
			std::this_thread::sleep_for(std::chrono::milliseconds{5});
		}

		REQUIRE(session.GetTurns().size() == turns);
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

TEST_CASE("The timeline follows the transcript and restarts with it, its version moving apart from ordinary growth")
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
		const Clock::time_point deadline = Clock::now() + std::chrono::seconds{5};
		while (timeline.GetRequests().size() != 2 && Clock::now() < deadline)
		{
			live.Take(session, timeline);
			std::this_thread::sleep_for(std::chrono::milliseconds{5});
		}

		REQUIRE(timeline.GetRequests().size() == 2);
	}

	const std::uint64_t versionBeforeRestart = timeline.GetVersion();

	{
		std::ofstream stream{file, std::ios::binary | std::ios::trunc};
		stream << Fixtures::UserPrompt("one") + Fixtures::Assistant("r1", Fixtures::TextBlock("a"), Fixtures::Usage(1, 1));
	}

	live.Poll();

	{
		const Clock::time_point deadline = Clock::now() + std::chrono::seconds{5};
		while (timeline.GetRequests().size() != 1 && Clock::now() < deadline)
		{
			live.Take(session, timeline);
			std::this_thread::sleep_for(std::chrono::milliseconds{5});
		}

		REQUIRE(timeline.GetRequests().size() == 1);
	}

	CHECK(timeline.GetVersion() != versionBeforeRestart);
}

TEST_CASE("A transcript that shrank is read again from nothing, under a generation of its own rather than a version that went backwards")
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

TEST_CASE("Twenty thousand lines arrive complete and once, however the polls interleave and even when a poll lands during a read")
{
	TempDirectory temp;
	const std::filesystem::path file = temp.MakeFile("s.jsonl", "");

	LiveTranscript live{file};
	Session session;

	constexpr std::size_t kTurns = 20'000;
	constexpr std::size_t kPollEveryTurns = 1000;

	for (std::size_t index = 0; index < kTurns; ++index)
	{
		Append(file, Fixtures::UserPrompt("p" + std::to_string(index)));

		if (index % kPollEveryTurns == 0)
		{
			live.Poll();
		}
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
	SubagentLinks links;

	live.Poll();
	WaitForTurns(live, session, 1);

	temp.MakeFile("p/s/subagents/agent-a1.jsonl", Fixtures::Assistant("r", Fixtures::TextBlock("y"), Fixtures::Usage(5, 6)));
	live.Poll();

	const Clock::time_point deadline = Clock::now() + std::chrono::seconds{5};
	while (!subagents.contains("a1") && Clock::now() < deadline)
	{
		live.TakeSubagents(subagents, links);
		std::this_thread::sleep_for(std::chrono::milliseconds{5});
	}

	REQUIRE(subagents.contains("a1"));

	CHECK(subagents.at("a1") == UsageByModel{{{"claude-opus-5", false}, {5, 6, 0, 0, 0}}});
}

namespace {
	void WaitForSubagent(LiveTranscript& live, SubagentUsage& subagents, const std::string& id, const UsageByModel& expected,
						 SubagentLinks* links = nullptr)
	{
		const Clock::time_point deadline = Clock::now() + std::chrono::seconds{5};
		SubagentLinks discarded;

		while ((!subagents.contains(id) || subagents.at(id) != expected) && Clock::now() < deadline)
		{
			live.TakeSubagents(subagents, links != nullptr ? *links : discarded);
			std::this_thread::sleep_for(std::chrono::milliseconds{5});
		}

		REQUIRE(subagents.contains(id));
		CHECK(subagents.at(id) == expected);
	}

	void WaitForLink(LiveTranscript& live, SubagentLinks& links, const std::string& toolUseId)
	{
		const Clock::time_point deadline = Clock::now() + std::chrono::seconds{5};
		SubagentUsage usage;

		while (!links.contains(toolUseId) && Clock::now() < deadline)
		{
			live.TakeSubagents(usage, links);
			std::this_thread::sleep_for(std::chrono::milliseconds{5});
		}

		REQUIRE(links.contains(toolUseId));
	}

	void WaitForWalk(LiveTranscript& live)
	{
		const Clock::time_point deadline = Clock::now() + std::chrono::seconds{5};

		while ((!live.GetProgress().FirstPassDone || !live.GetProgress().SubagentsWalked) && Clock::now() < deadline)
		{
			std::this_thread::sleep_for(std::chrono::milliseconds{5});
		}

		REQUIRE(live.GetProgress().FirstPassDone);
		REQUIRE(live.GetProgress().SubagentsWalked);
	}

	void BlankFirstLineInPlace(const std::filesystem::path& file, std::size_t blankedBytes)
	{
		std::fstream stream{file, std::ios::binary | std::ios::in | std::ios::out};
		stream.seekp(0);
		stream << std::string(blankedBytes, ' ');
	}
}

TEST_CASE("A subagent's transcript is followed from where the last read stopped, not read again from its start")
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

	BlankFirstLineInPlace(agent, first.size() - 1);

	Append(agent, Fixtures::Assistant("r3", Fixtures::TextBlock("w"), Fixtures::Usage(1, 1)));
	live.Poll();
	WaitForSubagent(live, subagents, "a1", {{{"claude-opus-5", false}, {7, 9, 0, 0, 0}}});
}

namespace {
	constexpr std::size_t kLargeTranscriptLines = 20'000;
	constexpr std::size_t kSubagentTranscriptLines = 150;
	constexpr int kSubagentFileCount = 80;
	constexpr int kDefaultProgressSeconds = 60;
	constexpr int kLargeReadSeconds = 300;
	constexpr int kMissingFileSeconds = 5;

	// WARNING: The bound on closing is a share of the time the same files take to read in full
	// in this very run, so it holds in Debug and Release and on a slow machine alike.
	constexpr int kFullReadDivisorForClosing = 4;

	LiveTranscript::LoadProgress WaitForProgress(const LiveTranscript& live,
												 const std::function<bool(const LiveTranscript::LoadProgress&)>& done,
												 int seconds = kDefaultProgressSeconds)
	{
		const Clock::time_point deadline = Clock::now() + std::chrono::seconds{seconds};

		LiveTranscript::LoadProgress progress = live.GetProgress();

		while (!done(progress) && Clock::now() < deadline)
		{
			std::this_thread::sleep_for(std::chrono::milliseconds{1});
			progress = live.GetProgress();
		}

		REQUIRE(done(progress));
		return progress;
	}

	bool AllRead(const LiveTranscript::LoadProgress& progress)
	{
		return progress.FirstPassDone && progress.SubagentsWalked;
	}

	bool FirstPassIsDone(const LiveTranscript::LoadProgress& progress)
	{
		return progress.FirstPassDone;
	}

	bool SomeBytesRead(const LiveTranscript::LoadProgress& progress)
	{
		return progress.Bytes > 0;
	}

	bool SubagentsBeingWalked(const LiveTranscript::LoadProgress& progress)
	{
		return progress.FirstPassDone && progress.SubagentsTotal > 0 && !progress.SubagentsWalked;
	}

	std::string LongTranscript(std::size_t lines)
	{
		std::string bytes;
		const std::string filler(300, 'x');

		for (std::size_t index = 0; index < lines; ++index)
		{
			bytes += Fixtures::UserPrompt(filler + std::to_string(index));
		}

		return bytes;
	}

	std::string SubagentTranscript(std::size_t lines)
	{
		std::string bytes;
		const std::string filler(300, 'y');

		for (std::size_t index = 0; index < lines; ++index)
		{
			bytes += Fixtures::Assistant("r" + std::to_string(index), Fixtures::TextBlock(filler), Fixtures::Usage(1, 1));
		}

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
	const LiveTranscript::LoadProgress progress = WaitForProgress(live, FirstPassIsDone, kMissingFileSeconds);

	CHECK(progress.Total == 0);
}

namespace {
	struct LargeSession
	{
		TempDirectory Temp;
		std::filesystem::path File;
		Clock::duration FullRead{};

		LargeSession()
		{
			File = Temp.MakeFile("p/s.jsonl", LongTranscript(kLargeTranscriptLines));

			const std::string subagent = SubagentTranscript(kSubagentTranscriptLines);
			for (int index = 0; index < kSubagentFileCount; ++index)
			{
				Temp.MakeFile("p/s/subagents/agent-a" + std::to_string(index) + ".jsonl", subagent);
			}

			LiveTranscript live{File};
			const Clock::time_point start = Clock::now();
			live.Poll();
			WaitForProgress(live, AllRead, kLargeReadSeconds);
			FullRead = Clock::now() - start;
		}
	};

	const LargeSession& GetLargeSession()
	{
		static const LargeSession session;
		return session;
	}

	Clock::duration TimeDestroy(std::unique_ptr<LiveTranscript>& live)
	{
		const Clock::time_point start = Clock::now();
		live.reset();
		return Clock::now() - start;
	}
}

TEST_CASE("Closing a transcript while its main file is read returns without finishing the read")
{
	const LargeSession& large = GetLargeSession();

	auto live = std::make_unique<LiveTranscript>(large.File);
	live->Poll();
	WaitForProgress(*live, SomeBytesRead);

	const LiveTranscript::LoadProgress before = live->GetProgress();
	const Clock::duration destroy = TimeDestroy(live);

	CHECK_FALSE(before.FirstPassDone);
	CHECK(destroy < large.FullRead / kFullReadDivisorForClosing);
}

TEST_CASE("Closing a transcript while its subagents are read returns without finishing the walk")
{
	const LargeSession& large = GetLargeSession();

	auto live = std::make_unique<LiveTranscript>(large.File);
	live->Poll();
	WaitForProgress(*live, SubagentsBeingWalked, kLargeReadSeconds);

	const LiveTranscript::LoadProgress before = live->GetProgress();
	const Clock::duration destroy = TimeDestroy(live);

	CHECK(before.SubagentsDone < before.SubagentsTotal);
	CHECK(destroy < large.FullRead / kFullReadDivisorForClosing);
}

TEST_CASE("A subagent's link arrives with its usage")
{
	TempDirectory temp;
	const std::filesystem::path file = temp.MakeFile("p/s.jsonl", Fixtures::UserPrompt("one"));
	temp.MakeFile("p/s/subagents/agent-a1.jsonl", Fixtures::Assistant("r", Fixtures::TextBlock("y"), Fixtures::Usage(5, 6)));
	temp.MakeFile("p/s/subagents/agent-a1.meta.json",
				  R"({"agentType":"Explore","description":"Find callers","toolUseId":"toolu_1"})");

	LiveTranscript live{file};
	SubagentLinks links;

	live.Poll();
	WaitForLink(live, links, "toolu_1");

	CHECK(links.at("toolu_1") == SubagentInfo{"a1", "Explore", "Find callers"});
}

TEST_CASE("A meta file written after its transcript, which has not moved, is linked on a later walk")
{
	TempDirectory temp;
	const std::filesystem::path file = temp.MakeFile("p/s.jsonl", Fixtures::UserPrompt("one"));
	temp.MakeFile("p/s/subagents/agent-a1.jsonl", Fixtures::Assistant("r", Fixtures::TextBlock("y"), Fixtures::Usage(5, 6)));

	LiveTranscript live{file};
	SubagentUsage usage;
	SubagentLinks links;

	live.Poll();
	WaitForSubagent(live, usage, "a1", {{{"claude-opus-5", false}, {5, 6, 0, 0, 0}}}, &links);
	WaitForWalk(live);
	CHECK(links.empty());

	temp.MakeFile("p/s/subagents/agent-a1.meta.json", R"({"toolUseId":"toolu_1"})");
	live.Poll();
	WaitForLink(live, links, "toolu_1");

	CHECK(links.at("toolu_1").AgentId == "a1");
}

TEST_CASE("A subagent whose meta file names no call is counted but not linked")
{
	TempDirectory temp;
	const std::filesystem::path file = temp.MakeFile("p/s.jsonl", Fixtures::UserPrompt("one"));
	temp.MakeFile("p/s/subagents/agent-a1.jsonl", Fixtures::Assistant("r", Fixtures::TextBlock("y"), Fixtures::Usage(5, 6)));
	temp.MakeFile("p/s/subagents/agent-a1.meta.json", "{not json");

	LiveTranscript live{file};
	SubagentUsage usage;
	SubagentLinks links;

	live.Poll();
	WaitForSubagent(live, usage, "a1", {{{"claude-opus-5", false}, {5, 6, 0, 0, 0}}}, &links);
	WaitForWalk(live);

	CHECK(links.empty());
}

TEST_CASE("A subagent's own transcript, followed live, finds no subagents of its own")
{
	TempDirectory temp;
	temp.MakeFile("p/s.jsonl", Fixtures::UserPrompt("one"));
	const std::filesystem::path agent =
		temp.MakeFile("p/s/subagents/agent-a1.jsonl", Fixtures::UserPrompt("task") +
														  Fixtures::Assistant("r", Fixtures::TextBlock("y"), Fixtures::Usage(5, 6)));

	LiveTranscript live{agent};
	Session session;

	live.Poll();
	WaitForTurns(live, session, 1);
	WaitForWalk(live);

	SubagentUsage usage;
	SubagentLinks links;
	live.TakeSubagents(usage, links);

	CHECK(session.GetTurns().front().Prompt == "task");
	CHECK(usage.empty());
	CHECK(links.empty());
	CHECK(live.GetProgress().SubagentsTotal == 0);
}
