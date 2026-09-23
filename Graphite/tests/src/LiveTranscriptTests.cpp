#include <doctest/doctest.h>

#include "Sessions/LiveTranscript.h"

#include "TempDirectory.h"
#include "TranscriptFixtures.h"

#include <chrono>
#include <fstream>
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
