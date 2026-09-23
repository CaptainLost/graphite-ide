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
	const std::vector<BucketCosts> turns = TurnCosts(session, pricing);

	REQUIRE(turns.size() == 2);

	UsageByModel all;
	for (const Turn& turn : session.GetTurns())
		for (const auto& [key, counts] : turn.Usage)
			all[key] += counts;

	CHECK(turns[0].Total() + turns[1].Total() == doctest::Approx(pricing.CostOf(all).Dollars));
}
