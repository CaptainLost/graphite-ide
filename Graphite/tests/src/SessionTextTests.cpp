#include <doctest/doctest.h>

#include "Sessions/Session.h"
#include "Sessions/SessionText.h"
#include "Sessions/TranscriptReader.h"

#include "TranscriptFixtures.h"

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
		for (const auto& [key, counts] : turn.Usage)
			all[key] += counts;

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
