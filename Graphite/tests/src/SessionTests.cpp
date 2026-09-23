#include <doctest/doctest.h>

#include "Sessions/Session.h"
#include "Sessions/TranscriptReader.h"

#include "TranscriptFixtures.h"

using namespace Graphite;

namespace {
	Session Build(const std::string& bytes)
	{
		Session session;
		session.Append(ParseTranscript(bytes).Records);
		return session;
	}
}

TEST_CASE("Each typed prompt starts a turn holding everything up to the next")
{
	const Session session = Build(
		Fixtures::UserPrompt("one", "2026-09-17T09:00:00.000Z") +
		Fixtures::Assistant("a", Fixtures::ThinkingBlock("think"), Fixtures::Usage(1, 1)) +
		Fixtures::Assistant("a", Fixtures::TextBlock("answer"), Fixtures::Usage(1, 1)) +
		Fixtures::UserPrompt("two", "2026-09-17T09:05:00.000Z") +
		Fixtures::Assistant("b", Fixtures::TextBlock("second"), Fixtures::Usage(2, 2)));

	REQUIRE(session.GetTurns().size() == 2);

	const Turn& first = session.GetTurns()[0];
	CHECK(first.Prompt == "one");
	CHECK(first.StartedAt == "2026-09-17T09:00:00.000Z");
	REQUIRE(first.Items.size() == 2);
	CHECK(first.Items[0].Kind == ItemKind::Thinking);
	CHECK(first.Items[1].Kind == ItemKind::Text);
	CHECK(first.Items[1].Text == "answer");
	CHECK(first.Usage == UsageByModel{{{"claude-opus-5", false}, {1, 1, 0, 0, 0}}});

	CHECK(session.GetTurns()[1].Prompt == "two");
}

TEST_CASE("A tool call is paired with its result, error flag and timing")
{
	const Session session = Build(
		Fixtures::UserPrompt("go") +
		Fixtures::Assistant("a", Fixtures::ToolUseBlock("t1", "Bash", {{"command", "ls"}}), Fixtures::Usage(1, 1),
							"claude-opus-5", "2026-09-17T09:00:01.000Z") +
		Fixtures::ToolResult("t1", "no such dir", true, "2026-09-17T09:00:03.500Z"));

	REQUIRE(session.GetTools().size() == 1);
	const ToolCall& tool = session.GetTools()[0];
	CHECK(tool.Name == "Bash");
	CHECK(tool.HasResult);
	CHECK(tool.IsError);
	CHECK(tool.Result == "no such dir");
	CHECK(tool.StartedAt == "2026-09-17T09:00:01.000Z");
	CHECK(tool.FinishedAt == "2026-09-17T09:00:03.500Z");

	REQUIRE(session.GetTurns().size() == 1);
	REQUIRE(session.GetTurns()[0].Items.size() == 1);
	CHECK(session.GetTurns()[0].Items[0].Kind == ItemKind::Tool);
	CHECK(session.GetTurns()[0].Items[0].Tool == 0);
}

TEST_CASE("A result arriving in a later append still finds its call")
{
	Session session;
	session.Append(ParseTranscript(Fixtures::UserPrompt("go") +
								   Fixtures::Assistant("a", Fixtures::ToolUseBlock("t1", "Read", {}),
													   Fixtures::Usage(1, 1)))
					   .Records);

	CHECK_FALSE(session.GetTools()[0].HasResult);
	const std::uint64_t before = session.GetVersion();

	session.Append(ParseTranscript(Fixtures::ToolResult("t1", "contents")).Records);

	CHECK(session.GetTools()[0].HasResult);
	CHECK(session.GetTools()[0].Result == "contents");
	CHECK(session.GetVersion() > before);
	CHECK(session.GetTurns().size() == 1);
}

TEST_CASE("Injected user text and tool results do not start turns")
{
	const Session session = Build(Fixtures::UserPrompt("go") + Fixtures::MetaUser("skill body") +
								  Fixtures::ToolResult("orphan", "x"));

	CHECK(session.GetTurns().size() == 1);
}

TEST_CASE("Replies before any prompt land in a turn with no prompt")
{
	const Session session = Build(Fixtures::Assistant("a", Fixtures::TextBlock("hello"), Fixtures::Usage(1, 1)));

	REQUIRE(session.GetTurns().size() == 1);
	CHECK(session.GetTurns()[0].Prompt.empty());
	CHECK(session.GetTurns()[0].Items.size() == 1);
}

TEST_CASE("Reset clears turns and tools and moves to a new generation")
{
	Session session;
	session.Append(ParseTranscript(Fixtures::UserPrompt("go") +
								   Fixtures::Assistant("a", Fixtures::ToolUseBlock("t1", "Read", {}),
													   Fixtures::Usage(1, 1)))
					   .Records);

	REQUIRE(session.GetTurns().size() == 1);
	REQUIRE(session.GetTools().size() == 1);
	const std::uint64_t generationBefore = session.GetGeneration();

	session.Reset();

	CHECK(session.GetTurns().empty());
	CHECK(session.GetTools().empty());
	CHECK(session.GetGeneration() != generationBefore);

	// A result for a tool the reset dropped must not find a stale entry: appending after a
	// reset behaves like appending to a session that never held it.
	session.Append(ParseTranscript(Fixtures::ToolResult("t1", "contents")).Records);
	CHECK(session.GetTools().empty());
}

TEST_CASE("An unknown block is kept as an item naming its type")
{
	const Session session =
		Build(Fixtures::UserPrompt("go") +
			  Fixtures::Assistant("a", {{"type", "server_tool_use"}}, Fixtures::Usage(1, 1)));

	REQUIRE(session.GetTurns()[0].Items.size() == 1);
	CHECK(session.GetTurns()[0].Items[0].Kind == ItemKind::Other);
	CHECK(session.GetTurns()[0].Items[0].Text == "server_tool_use");
}
