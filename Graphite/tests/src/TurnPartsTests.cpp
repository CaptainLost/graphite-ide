#include <doctest/doctest.h>

#include "Sessions/Session.h"
#include "Sessions/TranscriptReader.h"
#include "Sessions/TurnParts.h"

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

TEST_CASE("Consecutive tool calls fold into one step run between replies")
{
	const Session session = Build(
		Fixtures::UserPrompt("go") +
		Fixtures::Assistant("r1", Fixtures::TextBlock("Looking."), Fixtures::Usage(1, 1)) +
		Fixtures::Assistant("r2", Fixtures::ToolUseBlock("a", "Bash", {}), Fixtures::Usage(1, 1), "claude-opus-5",
							"2026-09-17T09:00:02.000Z") +
		Fixtures::ToolResult("a", "ok", false, "2026-09-17T09:00:03.000Z") +
		Fixtures::Assistant("r3", Fixtures::ToolUseBlock("b", "Bash", {}), Fixtures::Usage(1, 1), "claude-opus-5",
							"2026-09-17T09:00:04.000Z") +
		Fixtures::ToolResult("b", "no", true, "2026-09-17T09:00:07.000Z") +
		Fixtures::Assistant("r4", Fixtures::ToolUseBlock("c", "Read", {}), Fixtures::Usage(1, 1), "claude-opus-5",
							"2026-09-17T09:00:08.000Z") +
		Fixtures::ToolResult("c", "x", false, "2026-09-17T09:00:09.000Z") +
		Fixtures::Assistant("r5", Fixtures::TextBlock("Done."), Fixtures::Usage(1, 1)));

	const std::vector<TurnPart> parts = SplitTurn(session.GetTurns()[0], session.GetTools());

	REQUIRE(parts.size() == 3);
	CHECK(parts[0].Kind == PartKind::Reply);
	CHECK(parts[1].Kind == PartKind::Steps);
	CHECK(parts[1].Items.size() == 3);
	CHECK(parts[1].ToolCounts == std::vector<std::pair<std::string, std::size_t>>{{"Bash", 2}, {"Read", 1}});
	CHECK(parts[1].Failures == 1);
	CHECK(parts[1].Duration == std::chrono::milliseconds{7000});
	CHECK(parts[1].Requests == 3);
	CHECK(parts[1].RequestKeys == std::vector<std::string>{"r2", "r3", "r4"});
	CHECK(parts[2].Kind == PartKind::Reply);
}

TEST_CASE("Empty thinking is dropped, and its request's usage goes with the item after it")
{
	const Session session = Build(
		Fixtures::UserPrompt("go") +
		Fixtures::Assistant("r1", Fixtures::ThinkingBlock(""), Fixtures::Usage(100, 7)) +
		Fixtures::Assistant("r1", Fixtures::TextBlock("Answer."), Fixtures::Usage(100, 7)));

	const std::vector<TurnPart> parts = SplitTurn(session.GetTurns()[0], session.GetTools());

	REQUIRE(parts.size() == 1);
	CHECK(parts[0].Kind == PartKind::Reply);
	CHECK(parts[0].Requests == 1);
	CHECK(parts[0].Usage == UsageByModel{{{"claude-opus-5", false}, {100, 7, 0, 0, 0}}});
}

TEST_CASE("Thinking with text is a step, and an injected row stands on its own")
{
	const Session session = Build(
		Fixtures::UserPrompt("go") +
		Fixtures::Assistant("r1", Fixtures::ThinkingBlock("hmm"), Fixtures::Usage(1, 1)) +
		Fixtures::UserPrompt("<task-notification><summary>x</summary></task-notification>", "2026-09-17T09:00:05.000Z"));

	const std::vector<TurnPart> parts = SplitTurn(session.GetTurns()[0], session.GetTools());

	REQUIRE(parts.size() == 2);
	CHECK(parts[0].Kind == PartKind::Steps);
	CHECK(parts[0].Items.size() == 1);
	CHECK(parts[1].Kind == PartKind::Injected);
}

TEST_CASE("A pause longer than two minutes is a gap; a shorter one is not")
{
	CHECK(GapBetween("2026-09-17T09:00:00.000Z", "2026-09-17T09:06:00.000Z") == std::chrono::minutes{6});
	CHECK_FALSE(GapBetween("2026-09-17T09:00:00.000Z", "2026-09-17T09:01:59.000Z").has_value());
	CHECK_FALSE(GapBetween("", "2026-09-17T09:06:00.000Z").has_value());
}

TEST_CASE("A run of steps was last active when its last tool finished, not when it started")
{
	const Session session = Build(
		Fixtures::UserPrompt("go") +
		Fixtures::Assistant("r1", Fixtures::ToolUseBlock("a", "Bash", {}), Fixtures::Usage(1, 1), "claude-opus-5",
							"2026-09-17T09:00:02.000Z") +
		Fixtures::ToolResult("a", "ok", false, "2026-09-17T09:15:02.000Z") +
		Fixtures::Assistant("r2", Fixtures::TextBlock("Done."), Fixtures::Usage(1, 1), "claude-opus-5",
							"2026-09-17T09:16:02.000Z"));

	const Turn& turn = session.GetTurns()[0];
	const std::vector<TurnPart> parts = SplitTurn(turn, session.GetTools());

	REQUIRE(parts.size() == 2);
	REQUIRE(parts[0].Kind == PartKind::Steps);

	const std::string last = LastActivity(turn, parts[0], session.GetTools());
	CHECK(last == "2026-09-17T09:15:02.000Z");

	// The reply came a minute after the tool finished: no pause, however long the tool ran.
	CHECK_FALSE(GapBetween(last, turn.Items[parts[1].Items.front()].At).has_value());

	// A reply's own time is its last activity.
	CHECK(LastActivity(turn, parts[1], session.GetTools()) == "2026-09-17T09:16:02.000Z");
}

TEST_CASE("A tool still running leaves its start as the run's last activity")
{
	const Session session = Build(
		Fixtures::UserPrompt("go") +
		Fixtures::Assistant("r1", Fixtures::ToolUseBlock("a", "Bash", {}), Fixtures::Usage(1, 1), "claude-opus-5",
							"2026-09-17T09:00:02.000Z"));

	const Turn& turn = session.GetTurns()[0];
	const std::vector<TurnPart> parts = SplitTurn(turn, session.GetTools());

	REQUIRE(parts.size() == 1);
	CHECK(LastActivity(turn, parts[0], session.GetTools()) == "2026-09-17T09:00:02.000Z");
}

TEST_CASE("A queued message is a part of its own between the parts around it")
{
	const Session session = Build(
		Fixtures::UserPrompt("go") +
		Fixtures::Assistant("r1", Fixtures::ToolUseBlock("a", "Bash", {}), Fixtures::Usage(1, 1), "claude-opus-5",
							"2026-09-17T09:00:01.000Z") +
		Fixtures::ToolResult("a", "ok", false, "2026-09-17T09:00:02.000Z") +
		Fixtures::QueuedCommand("one", "2026-09-17T09:00:03.000Z") +
		Fixtures::QueuedCommand("two", "2026-09-17T09:00:03.500Z") +
		Fixtures::Assistant("r2", Fixtures::ToolUseBlock("b", "Read", {}), Fixtures::Usage(1, 1), "claude-opus-5",
							"2026-09-17T09:00:04.000Z") +
		Fixtures::ToolResult("b", "ok", false, "2026-09-17T09:00:05.000Z"));

	const std::vector<TurnPart> parts = SplitTurn(session.GetTurns()[0], session.GetTools());

	REQUIRE(parts.size() == 4);
	CHECK(parts[0].Kind == PartKind::Steps);
	CHECK(parts[1].Kind == PartKind::Queued);
	CHECK(parts[1].Items == std::vector<std::size_t>{1});
	CHECK(parts[2].Kind == PartKind::Queued);
	CHECK(parts[2].Items == std::vector<std::size_t>{2});
	CHECK(parts[3].Kind == PartKind::Steps);
	CHECK(parts[3].Requests == 1);
}

TEST_CASE("A part lists only the requests it counts")
{
	// A response written without usage is an item of the turn but not one of its requests:
	// listing its key would promise a request the part's count leaves out.
	const Session session = Build(
		Fixtures::UserPrompt("go") +
		Fixtures::Line({{"type", "assistant"},
						{"uuid", "a-r1"},
						{"requestId", "r1"},
						{"timestamp", "2026-09-17T09:00:01.000Z"},
						{"message", {{"role", "assistant"}, {"content", nlohmann::json::array({Fixtures::ToolUseBlock("a", "Bash", {})})}}}}) +
		Fixtures::Assistant("r2", Fixtures::ToolUseBlock("b", "Bash", {}), Fixtures::Usage(1, 1), "claude-opus-5",
							"2026-09-17T09:00:02.000Z"));

	const std::vector<TurnPart> parts = SplitTurn(session.GetTurns()[0], session.GetTools());

	REQUIRE(parts.size() == 1);
	CHECK(parts[0].Requests == 1);
	CHECK(parts[0].RequestKeys == std::vector<std::string>{"r2"});
}

TEST_CASE("A compaction stands on its own between two step runs")
{
	const Session session = Build(
		Fixtures::UserPrompt("go") +
		Fixtures::Assistant("r1", Fixtures::ToolUseBlock("a", "Bash", {}), Fixtures::Usage(1, 1)) +
		Fixtures::kCompactBoundary + Fixtures::kCompactSummary +
		Fixtures::Assistant("r2", Fixtures::ToolUseBlock("b", "Bash", {}), Fixtures::Usage(1, 1), "claude-opus-5",
							"2026-08-31T16:33:20.000Z"));

	const std::vector<TurnPart> parts = SplitTurn(session.GetTurns()[0], session.GetTools());

	REQUIRE(parts.size() == 3);
	CHECK(parts[0].Kind == PartKind::Steps);
	CHECK(parts[1].Kind == PartKind::Compaction);
	CHECK(parts[1].Items.size() == 1);
	CHECK(parts[1].Requests == 0);
	CHECK(parts[2].Kind == PartKind::Steps);
}
