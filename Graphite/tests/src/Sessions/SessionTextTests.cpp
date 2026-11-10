#include <doctest/doctest.h>

#include "Sessions/Requests/RequestTimeline.h"
#include "Sessions/Session.h"
#include "Sessions/SessionText.h"
#include "Sessions/Transcript/TranscriptReader.h"

#include "TranscriptFixtures.h"

#include <cstdint>
#include <optional>
#include <string>

using namespace Graphite;

TEST_CASE("A cost reads as dollars, a trace as less than a cent, and an unknown as a question mark")
{
	CHECK(FormatCost({4.1234, {}}) == "$4.12");
	CHECK(FormatCost({0.001, {}}) == "< $0.01");
	CHECK(FormatCost({0.0, {}}) == "$0.00");
	CHECK(FormatCost({3.0, {"mystery"}}) == "?");
}

TEST_CASE("A duration reads in its largest two units")
{
	using namespace std::chrono_literals;
	CHECK(FormatDuration(45s) == "45s");
	CHECK(FormatDuration(12min + 5s) == "12m");
	CHECK(FormatDuration(2h + 10min) == "2h 10m");
}

TEST_CASE("A tool's summary is the part of its input a reader looks for")
{
	CHECK(ToolSummary({.Name = "Bash", .InputJson = R"({"command":"ls -la","description":"List"})"}) == "ls -la");
	CHECK(ToolSummary({.Name = "Read", .InputJson = R"({"file_path":"D:\\a.cpp"})"}) == "D:\\a.cpp");
	CHECK(ToolSummary({.Name = "Grep", .InputJson = R"({"pattern":"TODO"})"}) == "TODO");
	CHECK(ToolSummary({.Name = "Agent", .InputJson = R"({"description":"Map panels","prompt":"..."})"}) == "Map panels");
	CHECK(ToolSummary({.Name = "Agent", .InputJson = R"({"description":"Map panels","subagent_type":"Explore"})"}) ==
		  "Map panels (Explore)");
	CHECK(ToolSummary({.Name = "Other", .InputJson = "not json"}) == "not json");
}

TEST_CASE("An edit lists its removed and added lines, and only an edit does")
{
	const ToolCall edit{.Name = "Edit", .InputJson = R"({"file_path":"a","old_string":"x\ny","new_string":"z"})"};

	CHECK(EditLines(edit) == std::vector<std::string>{"- x", "- y", "+ z"});
	CHECK(EditLines({.Name = "Bash", .InputJson = "{}"}).empty());
	CHECK(ToolFilePath(edit) == std::filesystem::path{"a"});
	CHECK_FALSE(ToolFilePath({.Name = "Bash", .InputJson = R"({"command":"x"})"}).has_value());
}

TEST_CASE("Per-turn costs add up to the session's cost")
{
	Session session;
	session.Append(ParseTranscript(Fixtures::UserPrompt("a") +
								   Fixtures::Assistant("r1", Fixtures::TextBlock("x"), Fixtures::Usage(1000, 2000)) +
								   Fixtures::UserPrompt("b", "2026-09-17T09:10:00.000Z") +
								   Fixtures::Assistant("r2", Fixtures::TextBlock("y"), Fixtures::Usage(3000, 4000, 500)))
					   .Records);

	const Pricing pricing = Pricing::Defaults();
	const std::vector<TurnCost> turns = TurnCosts(session, pricing);

	REQUIRE(turns.size() == 2);

	UsageByModel all;
	for (const Turn& turn : session.GetTurns())
	{
		for (const auto& [key, counts] : turn.Usage)
		{
			all[key] += counts;
		}
	}

	CHECK(turns[0].Known);
	CHECK(turns[1].Known);
	CHECK(turns[0].Buckets.Total() + turns[1].Buckets.Total() == doctest::Approx(pricing.CostOf(all).Dollars));
}

TEST_CASE("A turn that used a model with no price is reported as not fully priced")
{
	Session session;
	session.Append(ParseTranscript(Fixtures::UserPrompt("a") +
								   Fixtures::Assistant("r1", Fixtures::TextBlock("x"), Fixtures::Usage(1000, 2000)) +
								   Fixtures::UserPrompt("b", "2026-09-17T09:10:00.000Z") +
								   Fixtures::Assistant("r2", Fixtures::TextBlock("y"), Fixtures::Usage(3000, 4000),
													   "claude-mystery-9", "2026-09-17T09:10:01.000Z"))
					   .Records);

	const std::vector<TurnCost> turns = TurnCosts(session, Pricing::Defaults());

	REQUIRE(turns.size() == 2);
	CHECK(turns[0].Known);
	CHECK(turns[0].Buckets.Total() > 0.0);
	CHECK_FALSE(turns[1].Known);
}

namespace {
	std::string TwoTurnsTranscript()
	{
		return Fixtures::UserPrompt("a") +
			   Fixtures::Assistant("cheap", Fixtures::ToolUseBlock("t1", "Read", {{"file_path", "x"}}),
								   Fixtures::Usage(100, 10)) +
			   Fixtures::ToolResult("t1", "x") +
			   Fixtures::Assistant("dear", Fixtures::ToolUseBlock("t2", "Read", {{"file_path", "y"}}),
								   Fixtures::Usage(50000, 4000), "claude-opus-5", "2026-09-17T09:00:03.000Z") +
			   Fixtures::ToolResult("t2", "y", false, "2026-09-17T09:00:04.000Z") +
			   Fixtures::Assistant("edit", Fixtures::ToolUseBlock("t3", "Edit", {{"file_path", "y"}}),
								   Fixtures::Usage(2000, 200), "claude-opus-5", "2026-09-17T09:00:05.000Z") +
			   Fixtures::UserPrompt("b", "2026-09-17T09:10:00.000Z") +
			   Fixtures::Assistant("later", Fixtures::TextBlock("done"), Fixtures::Usage(3000, 300),
								   "claude-opus-5", "2026-09-17T09:10:01.000Z");
	}
}

TEST_CASE("A turn's requests are listed dearest first, each at its place in the timeline")
{
	const std::vector<TranscriptRecord> records = ParseTranscript(TwoTurnsTranscript()).Records;
	Session session;
	session.Append(records);
	RequestTimeline timeline;
	timeline.Append(records);

	const TurnBreakdown breakdown = BreakDownTurn(session, 0, timeline, Pricing::Defaults());

	REQUIRE(breakdown.Requests.size() == 3);
	CHECK(breakdown.Requests[0].Key == "dear");
	CHECK(breakdown.Requests[1].Key == "edit");
	CHECK(breakdown.Requests[2].Key == "cheap");
	CHECK(breakdown.Requests[0].Cost.Dollars > breakdown.Requests[1].Cost.Dollars);
	CHECK(breakdown.Requests[1].Cost.Dollars > breakdown.Requests[2].Cost.Dollars);

	for (const TurnRequest& request : breakdown.Requests)
	{
		REQUIRE(request.Position.has_value());
		CHECK(timeline.GetRequests()[*request.Position].Key == request.Key);
	}
}

TEST_CASE("A turn's tools are counted by name, the most used first")
{
	const std::vector<TranscriptRecord> records = ParseTranscript(TwoTurnsTranscript()).Records;
	Session session;
	session.Append(records);

	const TurnBreakdown first = BreakDownTurn(session, 0, RequestTimeline{}, Pricing::Defaults());

	REQUIRE(first.Tools.size() == 2);
	CHECK(first.Tools[0].Name == "Read");
	CHECK(first.Tools[0].Count == 2);
	CHECK(first.Tools[1].Name == "Edit");
	CHECK(first.Tools[1].Count == 1);

	REQUIRE(first.Requests.size() == 3);
	CHECK_FALSE(first.Requests[0].Position.has_value());

	const TurnBreakdown second = BreakDownTurn(session, 1, RequestTimeline{}, Pricing::Defaults());
	CHECK(second.Tools.empty());
	REQUIRE(second.Requests.size() == 1);
	CHECK(second.Requests[0].Key == "later");
}

TEST_CASE("A turn's request with no price comes last, however many tokens it had")
{
	Session session;
	session.Append(ParseTranscript(Fixtures::UserPrompt("a") +
								   Fixtures::Assistant("priced", Fixtures::TextBlock("x"), Fixtures::Usage(100, 10)) +
								   Fixtures::Assistant("mystery", Fixtures::TextBlock("y"), Fixtures::Usage(90000, 9000),
													   "claude-mystery-9", "2026-09-17T09:00:02.000Z"))
					   .Records);

	const TurnBreakdown breakdown = BreakDownTurn(session, 0, RequestTimeline{}, Pricing::Defaults());

	REQUIRE(breakdown.Requests.size() == 2);
	CHECK(breakdown.Requests[0].Key == "priced");
	CHECK(breakdown.Requests[1].Key == "mystery");
	CHECK_FALSE(breakdown.Requests[1].Cost.IsKnown());
}

TEST_CASE("A request's activity names each tool it called, else the start of its reply")
{
	RequestTimeline timeline;
	timeline.Append(
		ParseTranscript(
			Fixtures::UserPrompt("a") +
			Fixtures::Assistant("tools", Fixtures::ToolUseBlock("t1", "Read", {{"file_path", "D:\\a.cpp"}}),
								Fixtures::Usage(100, 10)) +
			Fixtures::Assistant("tools", Fixtures::ToolUseBlock("t2", "Bash", {{"command", "ls   -la"}}),
								Fixtures::Usage(100, 10), "claude-opus-5", "2026-09-17T09:00:01.500Z") +
			Fixtures::ToolResult("t1", "x") + Fixtures::ToolResult("t2", "y") +
			Fixtures::Assistant("reply", Fixtures::TextBlock("All done.\nThe rest."), Fixtures::Usage(100, 10),
								"claude-opus-5", "2026-09-17T09:00:03.000Z"))
			.Records);

	const std::vector<ApiRequest>& requests = timeline.GetRequests();
	REQUIRE(requests.size() == 2);

	const std::vector<std::string> tools = RequestActivity(requests[0]);
	REQUIRE(tools.size() == 2);
	CHECK(tools[0] == "Read D:\\a.cpp");
	CHECK(tools[1] == "Bash ls -la");

	const std::vector<std::string> reply = RequestActivity(requests[1]);
	REQUIRE(reply.size() == 1);
	CHECK(reply[0] == "\xe2\x80\x9c"
					  "All done. The rest."
					  "\xe2\x80\x9d");

	CHECK(RequestActivity(ApiRequest{}).empty());
}

TEST_CASE("A turn past the session's last has nothing to break down")
{
	const TurnBreakdown breakdown = BreakDownTurn(Session{}, 3, RequestTimeline{}, Pricing::Defaults());
	CHECK(breakdown.Requests.empty());
	CHECK(breakdown.Tools.empty());
}

TEST_CASE("A label is one line, never carries an ImGui id separator, and is cut on a character")
{
	CHECK(SingleLineLabel("fix the\n\n  build\tnow", 100) == "fix the build now");
	CHECK(SingleLineLabel("  padded  ", 100) == "padded");
	CHECK(SingleLineLabel("issue ##12 and ###3", 100) == "issue # #12 and # # #3");
	CHECK(SingleLineLabel("a\xc5\xbc\xc5\xbc", 4) == "a\xc5\xbc");
	CHECK(SingleLineLabel("", 10).empty());
}

TEST_CASE("A compaction's label joins every fact it has, with the time last")
{
	CompactionFacts facts;
	facts.Trigger = "auto";
	facts.PreTokens = 167324;
	facts.PostTokens = 5090;
	facts.Duration = std::chrono::milliseconds{169810};

	CHECK(CompactionLabel(facts, "18:33") ==
		  "Context compacted \xc2\xb7 167.3k \xe2\x86\x92 5.1k \xc2\xb7 auto \xc2\xb7 2m \xc2\xb7 18:33");
}

TEST_CASE("A compaction's label leaves out what it does not know, and the counts unless it has both")
{
	CompactionFacts facts;
	facts.PreTokens = 167324;

	CHECK(CompactionLabel(facts, "") == "Context compacted");
	CHECK(CompactionLabel(CompactionFacts{}, "18:33") == "Context compacted \xc2\xb7 18:33");
}

TEST_CASE("A turn's request costs are what the pricing makes of each request")
{
	const std::vector<TranscriptRecord> records = ParseTranscript(TwoTurnsTranscript()).Records;
	Session session;
	session.Append(records);
	RequestTimeline timeline;
	timeline.Append(records);
	const Pricing pricing = Pricing::Defaults();

	const TurnBreakdown breakdown = BreakDownTurn(session, 0, timeline, pricing);

	REQUIRE_FALSE(breakdown.Requests.empty());

	for (const TurnRequest& request : breakdown.Requests)
	{
		REQUIRE(request.Position.has_value());
		CHECK(request.Cost.Dollars ==
			  doctest::Approx(pricing.CostOf(timeline.GetRequests()[*request.Position]).Dollars));
	}
}

TEST_CASE("An unknown cost is explained by the models without a price")
{
	CHECK(UnknownCostHint({1.0, {}}).empty());
	CHECK(UnknownCostHint({1.0, {"mystery", "other"}}) ==
		  "No price for mystery, other. Add a price under Settings > Claude.");
}

TEST_CASE("An empty session list says why it is empty")
{
	CHECK(EmptyListMessage(true, 3, 10, true, "x") == "Reading transcripts... 3 / 10");
	CHECK(EmptyListMessage(true, 0, 0, false, "") == "Reading transcripts...");
	CHECK(EmptyListMessage(false, 0, 0, false, "bug") == "No sessions match \"bug\"");
	CHECK(EmptyListMessage(false, 0, 0, true, "") == "No sessions in this workspace");
	CHECK(EmptyListMessage(false, 0, 0, false, "") == "No sessions.");
}

TEST_CASE("A scan that has not started yet counts as scanning, so the list never says it is empty before it looked")
{
	constexpr bool kIndexing = true;
	constexpr bool kNotIndexing = false;
	constexpr bool kInitialScanRunning = true;
	constexpr bool kInitialScanDone = false;

	CHECK(SessionScanIsUnderway(kNotIndexing, kInitialScanDone));
	CHECK(SessionScanIsUnderway(kIndexing, kInitialScanRunning));
	CHECK_FALSE(SessionScanIsUnderway(kIndexing, kInitialScanDone));
}

TEST_CASE("The overview waits for the scan only when the cache gave nothing to draw")
{
	constexpr bool kIndexing = true;
	constexpr bool kNotIndexing = false;
	constexpr bool kInitialScanRunning = true;
	constexpr bool kInitialScanDone = false;
	constexpr bool kCacheGaveNothing = true;
	constexpr bool kCacheGaveSummaries = false;

	CHECK(OverviewWaitsForScan(kNotIndexing, kInitialScanDone, kCacheGaveNothing));
	CHECK(OverviewWaitsForScan(kIndexing, kInitialScanRunning, kCacheGaveNothing));
	CHECK_FALSE(OverviewWaitsForScan(kIndexing, kInitialScanRunning, kCacheGaveSummaries));
	CHECK_FALSE(OverviewWaitsForScan(kIndexing, kInitialScanDone, kCacheGaveNothing));
}

TEST_CASE("A pending session keeps waiting while Claude's terminal runs")
{
	const CommandTerminal running;

	CHECK_FALSE(PendingSessionEndText(running).has_value());
}

TEST_CASE("A pending session stops waiting when Claude exits first, and names the exit code")
{
	const CommandTerminal exitedWithCode{.Exited = true, .ExitCode = 1};
	const CommandTerminal exitedWithoutCode{.Exited = true};
	const CommandTerminal exitedThenClosed{.Exited = true, .ExitCode = 0, .Closed = true};

	CHECK(PendingSessionEndText(exitedWithCode) == std::optional<std::string>{"Claude exited before starting a session (code 1)"});
	CHECK(PendingSessionEndText(exitedWithoutCode) == std::optional<std::string>{"Claude exited before starting a session"});
	CHECK(PendingSessionEndText(exitedThenClosed) == std::optional<std::string>{"Claude exited before starting a session (code 0)"});
}

TEST_CASE("A pending session stops waiting when Claude's terminal is closed while it runs")
{
	const CommandTerminal closed{.Closed = true};

	CHECK(PendingSessionEndText(closed) == std::optional<std::string>{"Claude's terminal was closed before it started a session"});
}

TEST_CASE("A restored session that cannot be looked for says why")
{
	CHECK(UnfoundSessionText("No Claude transcripts at C:\\Users\\me\\.claude\\projects") ==
		  "This session can't be found: No Claude transcripts at C:\\Users\\me\\.claude\\projects");
}

TEST_CASE("The transcript loader names sizes only once the total is known")
{
	constexpr std::uint64_t kNothingYet = 0;
	constexpr std::uint64_t kOneAndAHalfMegabytes = 1572864;
	constexpr std::uint64_t kFourMegabytes = 4194304;

	CHECK(TranscriptLoadingText(kNothingYet, kNothingYet) == "Loading transcript");
	CHECK(TranscriptLoadingText(kOneAndAHalfMegabytes, kFourMegabytes) == "Loading transcript \xC2\xB7 1.5 / 4.0 MB");
}

TEST_CASE("An empty session is hidden from the list unless it is live or open")
{
	SessionSummary empty;
	empty.Id = "id";
	CHECK_FALSE(ShowsInSessionList(empty, false));
	CHECK(ShowsInSessionList(empty, true));

	SessionSummary titled = empty;
	titled.Title = "t";
	CHECK(ShowsInSessionList(titled, false));

	SessionSummary prompted = empty;
	prompted.FirstPrompt = "hello";
	CHECK(ShowsInSessionList(prompted, false));

	SessionSummary used = empty;
	used.Subagents[{"claude-opus-5", false}] = TokenCounts{1, 0, 0, 0, 0};
	CHECK(ShowsInSessionList(used, false));

	SessionSummary zeroUsage = empty;
	zeroUsage.Main[{"claude-opus-5", false}] = TokenCounts{};
	CHECK_FALSE(ShowsInSessionList(zeroUsage, false));
}

TEST_CASE("The live session mark is the UTF-8 encoding of a black circle")
{
	CHECK(kLiveSessionMark == std::string_view{"\xE2\x97\x8F", 3});
}
