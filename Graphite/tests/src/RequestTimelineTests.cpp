#include <doctest/doctest.h>

#include "Sessions/RequestTimeline.h"
#include "Sessions/TranscriptReader.h"

#include "TranscriptFixtures.h"

using namespace Graphite;

namespace {
	std::vector<ApiRequest> Build(const std::string& bytes)
	{
		return BuildRequestTimeline(ParseTranscript(bytes).Records);
	}

	// An assistant record with neither a requestId nor a uuid: Fixtures::Assistant always
	// fills in a uuid, so a truly keyless record has to be built by hand.
	std::string KeylessAssistant(const nlohmann::json& block, const nlohmann::json& usage,
								 const std::string& timestamp = "2026-09-17T09:00:01.000Z")
	{
		return Fixtures::Line({{"type", "assistant"},
							   {"timestamp", timestamp},
							   {"cwd", "D:\\Repos\\IDE"},
							   {"message",
								{{"role", "assistant"},
								 {"content", nlohmann::json::array({block})},
								 {"usage", usage}}}});
	}

	// An assistant record with a requestId but no usage at all, unlike Fixtures::Assistant
	// which always carries one.
	std::string AssistantNoUsage(const std::string& requestId, const nlohmann::json& block,
								 const std::string& timestamp = "2026-09-17T09:00:01.000Z")
	{
		return Fixtures::Line({{"type", "assistant"},
							   {"uuid", "assistant-" + requestId + "-" + timestamp},
							   {"requestId", requestId},
							   {"timestamp", timestamp},
							   {"cwd", "D:\\Repos\\IDE"},
							   {"message",
								{{"role", "assistant"}, {"content", nlohmann::json::array({block})}}}});
	}

	// A record of an existing request that carries no blocks and no usage, only a stop
	// reason — the shape a final housekeeping record for a response can take.
	std::string AssistantStopReasonOnly(const std::string& requestId, const std::string& stopReason,
										const std::string& timestamp)
	{
		return Fixtures::Line({{"type", "assistant"},
							   {"uuid", "assistant-" + requestId + "-" + timestamp},
							   {"requestId", requestId},
							   {"timestamp", timestamp},
							   {"cwd", "D:\\Repos\\IDE"},
							   {"message",
								{{"role", "assistant"},
								 {"content", nlohmann::json::array()},
								 {"stop_reason", stopReason}}}});
	}
}

TEST_CASE("A response written as several records is one request, and what preceded it is what was sent")
{
	const std::vector<ApiRequest> requests = Build(
		Fixtures::UserPrompt("go", "2026-09-17T09:00:00.000Z") +
		Fixtures::Assistant("r1", Fixtures::ThinkingBlock(""), Fixtures::Usage(10, 5, 0, 0, 90), "claude-opus-5", "2026-09-17T09:00:04.000Z") +
		Fixtures::Assistant("r1", Fixtures::ToolUseBlock("t1", "Bash", {{"command", "ls"}}), Fixtures::Usage(10, 5, 0, 0, 90), "claude-opus-5",
							"2026-09-17T09:00:04.000Z", "tool_use"));

	REQUIRE(requests.size() == 1);
	const ApiRequest& request = requests[0];
	CHECK(request.Index == 1);
	CHECK(request.Key == "r1");
	CHECK(request.Context == 100);
	CHECK_FALSE(request.ContextDelta.has_value());
	CHECK(request.CachedShare == doctest::Approx(0.9));
	CHECK(request.StopReason == "tool_use");
	CHECK(request.Waited == std::chrono::milliseconds{4000});
	REQUIRE(request.Sent.size() == 1);
	CHECK(request.Sent[0].Kind == SentKind::Prompt);
	REQUIRE(request.Response.size() == 2);
	CHECK(request.Response[0].Kind == ResponseKind::Thinking);
	CHECK(request.Response[1].Kind == ResponseKind::ToolCall);
	CHECK(request.Response[1].ToolName == "Bash");
}

TEST_CASE("The next request's sent items are the tool result and what came with it, with estimates that sum to the delta")
{
	const std::vector<ApiRequest> requests = Build(
		Fixtures::UserPrompt("go") +
		Fixtures::Assistant("r1", Fixtures::ToolUseBlock("t1", "Read", {{"file_path", "a.cpp"}}), Fixtures::Usage(0, 5, 0, 0, 1000)) +
		Fixtures::ToolResultWith("t1", std::string(300, 'x'), {{"file", {{"numLines", 3}}}}) +
		Fixtures::Attachment({{"type", "total_tokens_reminder"}, {"text", "tokens"}}) +
		Fixtures::Assistant("r2", Fixtures::TextBlock("done"), Fixtures::Usage(0, 7, 0, 0, 1200), "claude-opus-5",
							"2026-09-17T09:00:09.000Z", "end_turn"));

	REQUIRE(requests.size() == 2);
	const ApiRequest& second = requests[1];
	REQUIRE(second.ContextDelta.has_value());
	CHECK(*second.ContextDelta == 200);
	REQUIRE(second.Sent.size() == 2);
	CHECK(second.Sent[0].Kind == SentKind::ToolResult);
	CHECK(second.Sent[0].ToolName == "Read");
	CHECK(second.Sent[0].Label.starts_with("Read"));
	CHECK(second.Sent[1].Kind == SentKind::Reminder);
	CHECK(second.Sent[1].Label == "total_tokens_reminder");
	CHECK(second.Sent[0].EstimatedTokens + second.Sent[1].EstimatedTokens == 200);
	CHECK(second.Sent[0].EstimatedTokens > second.Sent[1].EstimatedTokens);
	CHECK(ToolNamesUsed(second) == std::vector<std::string>{"Read"});
}

TEST_CASE("A context that shrank gives no estimates, and a large cache write marks a rebuilt cache")
{
	const std::vector<ApiRequest> requests = Build(
		Fixtures::Assistant("r1", Fixtures::TextBlock("a"), Fixtures::Usage(0, 1, 0, 0, 5000)) +
		Fixtures::UserPrompt("again", "2026-09-17T09:10:00.000Z") +
		Fixtures::Assistant("r2", Fixtures::TextBlock("b"), Fixtures::Usage(10, 1, 3000, 0, 10), "claude-opus-5", "2026-09-17T09:10:02.000Z"));

	const ApiRequest& second = requests[1];
	CHECK(*second.ContextDelta == 3020 - 5000);
	CHECK(second.Sent[0].EstimatedTokens == 0);
	CHECK(second.CacheRebuilt);
	CHECK_FALSE(requests[0].CacheRebuilt);
}

TEST_CASE("Every kind of attachment and user text is classified")
{
	const std::vector<ApiRequest> requests = Build(
		Fixtures::Attachment({{"type", "deferred_tools_delta"}, {"addedNames", {"WebFetch", "Monitor"}}}) +
		Fixtures::Attachment({{"type", "skill_listing"}, {"names", {"a", "b"}}}) +
		Fixtures::Attachment({{"type", "mcp_instructions_delta"}, {"addedNames", {"ghidra"}}}) +
		Fixtures::Attachment({{"type", "edited_text_file"}, {"filename", "D:\\a.cpp"}}) +
		Fixtures::Attachment({{"type", "queued_command"}, {"prompt", "also this"}, {"origin", {{"kind", "human"}}}}) +
		Fixtures::MetaUser("skill body") +
		Fixtures::UserPrompt("<system-reminder>be brief</system-reminder>") +
		Fixtures::UserPrompt("real prompt") +
		Fixtures::Assistant("r1", Fixtures::TextBlock("ok"), Fixtures::Usage(1, 1)));

	std::vector<SentKind> kinds;
	for (const SentItem& item : requests[0].Sent)
		kinds.push_back(item.Kind);

	CHECK(kinds == std::vector<SentKind>{SentKind::ToolsLoaded, SentKind::Skills, SentKind::McpInstructions, SentKind::EditedFile,
										 SentKind::QueuedPrompt, SentKind::Injected, SentKind::Injected, SentKind::Prompt});
	CHECK(requests[0].Sent[0].Label == "+2 tools: WebFetch, Monitor");
	CHECK(requests[0].Sent[1].Label == "2 skills");
	CHECK(requests[0].Sent[2].Label == "MCP: ghidra");
	CHECK(requests[0].Sent[3].Label == "Edited D:\\a.cpp");
	CHECK(requests[0].ToolsAvailable == 2);
}

TEST_CASE("Tools available follow additions and removals")
{
	const std::vector<ApiRequest> requests = Build(
		Fixtures::Attachment({{"type", "deferred_tools_delta"}, {"addedNames", {"A", "B", "C"}}}) +
		Fixtures::Assistant("r1", Fixtures::TextBlock("x"), Fixtures::Usage(1, 1)) +
		Fixtures::Attachment({{"type", "deferred_tools_delta"}, {"addedNames", {"D"}}, {"removedNames", {"A", "B"}}}) +
		Fixtures::Assistant("r2", Fixtures::TextBlock("y"), Fixtures::Usage(1, 1)));

	CHECK(requests[0].ToolsAvailable == 3);
	CHECK(requests[1].ToolsAvailable == 2);
	CHECK(requests[1].Sent[0].Label == "+1 tools: D, \xe2\x88\x92" "2 removed");
}

TEST_CASE("Appending in batches gives the same requests as appending at once, even with a request split across them")
{
	const std::vector<TranscriptRecord> records = ParseTranscript(
		Fixtures::UserPrompt("go") +
		Fixtures::Assistant("r1", Fixtures::ToolUseBlock("t1", "Grep", {{"pattern", "x"}}), Fixtures::Usage(0, 1, 0, 0, 100)) +
		Fixtures::Assistant("r1", Fixtures::TextBlock("and"), Fixtures::Usage(0, 1, 0, 0, 100)) +
		Fixtures::ToolResult("t1", "hit") +
		Fixtures::Assistant("r2", Fixtures::TextBlock("done"), Fixtures::Usage(0, 1, 0, 0, 150))).Records;

	RequestTimeline whole;
	whole.Append(records);

	RequestTimeline split;
	for (const TranscriptRecord& record : records)
		split.Append({record});

	REQUIRE(split.GetRequests().size() == 2);
	CHECK(split.GetRequests()[0].Response.size() == 2);
	CHECK(split.GetRequests()[1].Sent[0].ToolName == "Grep");
	CHECK(split.GetRequests().size() == whole.GetRequests().size());
	CHECK(split.IndexOf("r2") == std::optional<std::size_t>{1});
	CHECK(split.GetVersion() > 0);
}

TEST_CASE("Reset forgets everything")
{
	RequestTimeline timeline;
	timeline.Append(ParseTranscript(Fixtures::Assistant("r1", Fixtures::TextBlock("x"), Fixtures::Usage(1, 1))).Records);
	timeline.Reset();

	CHECK(timeline.GetRequests().empty());
	CHECK_FALSE(timeline.IndexOf("r1").has_value());
}

TEST_CASE("Two keyless assistant records separated by a user prompt give two requests, each with its own Sent")
{
	const std::vector<ApiRequest> requests = Build(
		KeylessAssistant(Fixtures::TextBlock("a"), Fixtures::Usage(1, 1), "2026-09-17T09:00:01.000Z") +
		Fixtures::UserPrompt("next", "2026-09-17T09:00:02.000Z") +
		KeylessAssistant(Fixtures::TextBlock("b"), Fixtures::Usage(1, 1), "2026-09-17T09:00:03.000Z"));

	REQUIRE(requests.size() == 2);
	CHECK(requests[0].Key.empty());
	CHECK(requests[0].Sent.empty());
	REQUIRE(requests[1].Sent.size() == 1);
	CHECK(requests[1].Sent[0].Kind == SentKind::Prompt);
	CHECK(requests[1].Sent[0].Label == "next");
}

TEST_CASE("A request whose first record lacks usage but whose second has it shows the second's context")
{
	const std::vector<ApiRequest> requests = Build(
		AssistantNoUsage("r1", Fixtures::TextBlock("a"), "2026-09-17T09:00:01.000Z") +
		Fixtures::Assistant("r1", Fixtures::TextBlock("b"), Fixtures::Usage(10, 0, 0, 0, 90), "claude-opus-5",
							"2026-09-17T09:00:01.000Z"));

	REQUIRE(requests.size() == 1);
	CHECK(requests[0].Context == 100);
	CHECK(requests[0].CachedShare == doctest::Approx(0.9));
}

TEST_CASE("A no-usage request between two usage requests does not inflate the third's delta")
{
	const std::vector<ApiRequest> requests = Build(
		Fixtures::Assistant("r1", Fixtures::TextBlock("a"), Fixtures::Usage(0, 1, 0, 0, 1000)) +
		AssistantNoUsage("r2", Fixtures::TextBlock("b"), "2026-09-17T09:00:02.000Z") +
		Fixtures::Assistant("r3", Fixtures::TextBlock("c"), Fixtures::Usage(0, 1, 0, 0, 1300), "claude-opus-5",
							"2026-09-17T09:00:03.000Z"));

	REQUIRE(requests.size() == 3);
	REQUIRE(requests[2].ContextDelta.has_value());
	CHECK(*requests[2].ContextDelta == 1300 - 1000);
}

TEST_CASE("A same-key record that changes only the stop reason bumps the version")
{
	RequestTimeline timeline;
	timeline.Append(ParseTranscript(Fixtures::Assistant("r1", Fixtures::TextBlock("a"), Fixtures::Usage(1, 1))).Records);
	const std::uint64_t before = timeline.GetVersion();

	timeline.Append(ParseTranscript(AssistantStopReasonOnly("r1", "end_turn", "2026-09-17T09:00:02.000Z")).Records);

	CHECK(timeline.GetVersion() > before);
	CHECK(timeline.GetRequests()[0].StopReason == "end_turn");
}

TEST_CASE("An unparseable timestamp does not hide the last good one for Waited")
{
	const std::vector<ApiRequest> requests = Build(
		Fixtures::Assistant("r1", Fixtures::TextBlock("a"), Fixtures::Usage(1, 1), "claude-opus-5",
							"2026-09-17T09:00:00.000Z") +
		Fixtures::Attachment({{"type", "total_tokens_reminder"}}, "not-a-timestamp") +
		Fixtures::Assistant("r2", Fixtures::TextBlock("b"), Fixtures::Usage(1, 1), "claude-opus-5",
							"2026-09-17T09:00:05.000Z"));

	REQUIRE(requests.size() == 2);
	CHECK(requests[1].Waited == std::chrono::milliseconds{5000});
}
