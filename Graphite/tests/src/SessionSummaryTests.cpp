#include <doctest/doctest.h>

#include "Sessions/SessionSummary.h"
#include "Sessions/TranscriptReader.h"

#include "TempDirectory.h"
#include "TranscriptFixtures.h"

#include <fstream>

using namespace Graphite;

TEST_CASE("One request written as three records is counted once")
{
	const TokenCounts usage{10, 20, 30, 0, 40};
	const std::string bytes =
		Fixtures::Assistant("req-1", Fixtures::ThinkingBlock("hm"), Fixtures::Usage(10, 20, 30, 0, 40)) +
		Fixtures::Assistant("req-1", Fixtures::TextBlock("ok"), Fixtures::Usage(10, 20, 30, 0, 40)) +
		Fixtures::Assistant("req-1", Fixtures::ToolUseBlock("t", "Bash", {}), Fixtures::Usage(10, 20, 30, 0, 40));

	UsageTally tally;
	for (const TranscriptRecord& record : ParseTranscript(bytes).Records)
	{
		tally.Add(record);
	}

	CHECK(tally.Get() == UsageByModel{{{"claude-opus-5", false}, usage}});
}

TEST_CASE("A response written while it streamed is counted with its whole output, not the first record's")
{
	UsageTally tally;
	for (const TranscriptRecord& record : ParseTranscript(Fixtures::kStreamedResponse).Records)
	{
		tally.Add(record);
	}

	CHECK(tally.Get() == UsageByModel{{{"claude-opus-5", false}, TokenCounts{2, 657, 304, 0, 67877}}});
}

TEST_CASE("A refused response keeps what the model's record cost when Claude Code's zero record follows it")
{
	UsageTally tally;
	for (const TranscriptRecord& record : ParseTranscript(Fixtures::kRefusedResponse).Records)
	{
		tally.Add(record);
	}

	CHECK(tally.Get() == UsageByModel{{{"claude-opus-4-8", false}, TokenCounts{2, 1066, 0, 17298, 28871}}});
}

TEST_CASE("Two requests are both counted, each under its own model")
{
	const std::string bytes =
		Fixtures::Assistant("a", Fixtures::TextBlock("x"), Fixtures::Usage(1, 2), "claude-opus-5") +
		Fixtures::Assistant("b", Fixtures::TextBlock("y"), Fixtures::Usage(3, 4), "claude-haiku-4-5");

	UsageTally tally;
	for (const TranscriptRecord& record : ParseTranscript(bytes).Records)
	{
		tally.Add(record);
	}

	CHECK(tally.Get().at({"claude-opus-5", false}) == TokenCounts{1, 2, 0, 0, 0});
	CHECK(tally.Get().at({"claude-haiku-4-5", false}) == TokenCounts{3, 4, 0, 0, 0});
}

TEST_CASE("The summary takes its title from the last ai-title and its span from the timestamps")
{
	TempDirectory temp;
	const std::filesystem::path file = temp.MakeFile(
		"proj/abc.jsonl",
		Fixtures::UserPrompt("Fix it", "2026-09-17T09:00:00.000Z") + Fixtures::AiTitle("First title") +
			Fixtures::Assistant("r", Fixtures::TextBlock("ok"), Fixtures::Usage(1, 1), "claude-opus-5",
								"2026-09-17T10:30:00.000Z") +
			Fixtures::AiTitle("Better title") + "{broken\n");

	const SessionSummary summary = SummariseSession(file);

	CHECK(summary.Id == "abc");
	CHECK(summary.Title == "Better title");
	CHECK(summary.FirstPrompt == "Fix it");
	CHECK(summary.Cwd == std::filesystem::path{"D:\\Repos\\IDE"});
	CHECK(summary.StartedAt == "2026-09-17T09:00:00.000Z");
	CHECK(summary.EndedAt == "2026-09-17T10:30:00.000Z");
	CHECK(summary.SkippedLines == 1);
	CHECK(summary.DisplayTitle() == "Better title");
}

TEST_CASE("Without a title the first typed prompt names the session, skipping injected text")
{
	TempDirectory temp;
	const std::filesystem::path file =
		temp.MakeFile("proj/abc.jsonl", Fixtures::MetaUser("skill body") +
											Fixtures::UserPrompt("<command-name>/clear</command-name>") +
											Fixtures::UserPrompt("Real question"));

	const SessionSummary summary = SummariseSession(file);

	CHECK(summary.Title.empty());
	CHECK(summary.DisplayTitle() == "Real question");
}

TEST_CASE("Subagents are tallied apart from the session and counted")
{
	TempDirectory temp;
	const std::filesystem::path file = temp.MakeFile(
		"proj/abc.jsonl", Fixtures::Assistant("main", Fixtures::TextBlock("x"), Fixtures::Usage(1, 1)));
	temp.MakeFile("proj/abc/subagents/agent-1.jsonl",
				  Fixtures::Assistant("sub-1", Fixtures::TextBlock("y"), Fixtures::Usage(5, 5), "claude-sonnet-5"));
	temp.MakeFile("proj/abc/subagents/agent-2.jsonl",
				  Fixtures::Assistant("sub-2", Fixtures::TextBlock("z"), Fixtures::Usage(7, 7), "claude-sonnet-5"));
	temp.MakeFile("proj/abc/subagents/agent-1.meta.json", "{}");

	const SessionSummary summary = SummariseSession(file);

	CHECK(summary.Main == UsageByModel{{{"claude-opus-5", false}, {1, 1, 0, 0, 0}}});
	CHECK(summary.Subagents == UsageByModel{{{"claude-sonnet-5", false}, {12, 12, 0, 0, 0}}});
	CHECK(summary.SubagentCount == 2);
}

TEST_CASE("Appending to a subagent changes the session's fingerprint")
{
	TempDirectory temp;
	const std::filesystem::path file = temp.MakeFile("proj/abc.jsonl", Fixtures::UserPrompt("x"));
	const std::filesystem::path agent = temp.MakeFile("proj/abc/subagents/agent-1.jsonl", Fixtures::UserPrompt("y"));

	const Fingerprint before = FingerprintOf(file);

	{
		std::ofstream stream{agent, std::ios::binary | std::ios::app};
		stream << Fixtures::UserPrompt("more");
	}

	CHECK_FALSE(FingerprintOf(file) == before);
	CHECK(before.Files == 2);
}

TEST_CASE("A session in the workspace or below it belongs to the workspace")
{
	CHECK(IsWithinDirectory("D:\\Repos\\IDE", "D:\\Repos\\IDE"));
	CHECK(IsWithinDirectory("d:\\repos\\ide\\", "D:\\Repos\\IDE"));
	CHECK(IsWithinDirectory("D:/Repos/IDE/Graphite", "D:\\Repos\\IDE"));
	CHECK_FALSE(IsWithinDirectory("D:\\Repos\\IDE2", "D:\\Repos\\IDE"));
	CHECK_FALSE(IsWithinDirectory("D:\\Repos", "D:\\Repos\\IDE"));
	CHECK_FALSE(IsWithinDirectory("", "D:\\Repos\\IDE"));
}

TEST_CASE("A timestamp parses to the millisecond")
{
	const auto parsed = ParseTimestamp("2026-09-17T09:29:50.246Z");
	const auto later = ParseTimestamp("2026-09-17T09:29:51.000Z");

	REQUIRE(parsed.has_value());
	REQUIRE(later.has_value());
	CHECK((*later - *parsed).count() == 754);
	CHECK_FALSE(ParseTimestamp("yesterday").has_value());
}

TEST_CASE("Truncating never cuts a character in half")
{
	// "ż" is two bytes; a limit of 3 falls inside the second one.
	CHECK(TruncateUtf8("a\xc5\xbc\xc5\xbc", 4) == "a\xc5\xbc");
	CHECK(TruncateUtf8("short", 100) == "short");
}

TEST_CASE("A transcript that cannot be read has no summary, and a readable one has")
{
	TempDirectory temp;
	const std::filesystem::path file = temp.MakeFile("p/a.jsonl", Fixtures::UserPrompt("hello"));
	const std::filesystem::path directory = temp.MakeDir("p/x.jsonl");

	const std::optional<SessionSummary> read = TrySummariseSession(file);
	REQUIRE(read.has_value());
	CHECK(read->FirstPrompt == "hello");

	CHECK_FALSE(TrySummariseSession(directory).has_value());
	CHECK_FALSE(TrySummariseSession(temp.GetPath() / "p" / "missing.jsonl").has_value());
}

TEST_CASE("Folders that differ only in case or a trailing separator share one key")
{
	CHECK(DirectoryKey("d:\\repos\\ide\\") == DirectoryKey("D:\\Repos\\IDE"));
	CHECK(DirectoryKey("D:/Repos/IDE") == DirectoryKey("D:\\Repos\\IDE"));
	CHECK_FALSE(DirectoryKey("D:\\Repos\\IDE2") == DirectoryKey("D:\\Repos\\IDE"));
	CHECK(DirectoryKey("").empty());
}

TEST_CASE("Session ids match without regard to case")
{
	CHECK(SameSessionId("0a1b2c3d-EEFF", "0A1B2C3D-eeff"));
	CHECK_FALSE(SameSessionId("0a1b", "0a1c"));
	CHECK_FALSE(SameSessionId("0a1b", "0a1b2"));
}

TEST_CASE("A prompt starting with markup can name the session; a notification cannot")
{
	TempDirectory temp;
	const std::filesystem::path file = temp.MakeFile(
		"proj/abc.jsonl", Fixtures::UserPrompt("<task-notification><summary>x</summary></task-notification>") +
							  Fixtures::UserPrompt("<div> does not render"));

	CHECK(SummariseSession(file).DisplayTitle() == "<div> does not render");
}
