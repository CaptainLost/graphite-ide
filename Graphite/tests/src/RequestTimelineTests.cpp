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

	// A prompt snapshot as Claude Code writes the pair of them: the first before request #1
	// with the system prompt alone, the second after it with the tools and the CLI prefix.
	std::string Snapshot(bool withTools)
	{
		nlohmann::json attachment = {{"type", "prompt_snapshot"}, {"systemPrompt", nlohmann::json::array({"Be helpful."})}};

		if (withTools)
		{
			attachment["cliPrefix"] = "Prefix.";
			attachment["tools"] = nlohmann::json::array(
				{nlohmann::json{{"name", "Read"},
								{"description", "Reads."},
								{"schema", {{"name", "Read"}, {"description", "Reads."}, {"input_schema", {{"type", "object"}}}}}}});
		}

		return Fixtures::VersionedAttachment(attachment, "2.1.280");
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

TEST_CASE("The next request's sent items are the tool result and what came with it")
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
	CHECK(second.Sent[0].Bytes == 300);
	CHECK(second.Sent[1].Kind == SentKind::Reminder);
	CHECK(second.Sent[1].Label == "total_tokens_reminder");
	CHECK(ToolNamesUsed(second) == std::vector<std::string>{"Read"});
}

TEST_CASE("A large cache write marks a rebuilt cache")
{
	const std::vector<ApiRequest> requests = Build(
		Fixtures::Assistant("r1", Fixtures::TextBlock("a"), Fixtures::Usage(0, 1, 0, 0, 5000)) +
		Fixtures::UserPrompt("again", "2026-09-17T09:10:00.000Z") +
		Fixtures::Assistant("r2", Fixtures::TextBlock("b"), Fixtures::Usage(10, 1, 3000, 0, 10), "claude-opus-5", "2026-09-17T09:10:02.000Z"));

	const ApiRequest& second = requests[1];
	CHECK(*second.ContextDelta == 3020 - 5000);
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
	CHECK(requests[0].Sent[0].Label == "2 deferred tool names: WebFetch, Monitor");
	CHECK(requests[0].Sent[1].Label == "2 skills");
	CHECK(requests[0].Sent[2].Label == "MCP: ghidra");
	CHECK(requests[0].Sent[3].Label == "Edited D:\\a.cpp");
	CHECK(requests[0].ToolsAvailable == 2);

	CHECK(requests[0].Sent[4].Label == "also this");
	CHECK(requests[0].Sent[4].Text == "also this");
	CHECK(requests[0].Sent[5].Text == "skill body");
	CHECK(requests[0].Sent[6].Text == "<system-reminder>be brief</system-reminder>");
	CHECK(requests[0].Sent[7].Text == "real prompt");
}

TEST_CASE("A queued prompt with a pasted image is listed by its words, and its text names the image")
{
	const nlohmann::json blocks = nlohmann::json::array(
		{Fixtures::TextBlock("look at this [Image #2]"), Fixtures::ImageBlock("image/png", std::string(4096, 'A'))});
	const std::vector<ApiRequest> requests = Build(
		Fixtures::VersionedAttachment(
			{{"type", "queued_command"}, {"prompt", blocks}, {"origin", {{"kind", "human"}}}, {"commandMode", "prompt"}},
			"2.1.261") +
		Fixtures::Assistant("r1", Fixtures::TextBlock("ok"), Fixtures::Usage(1, 1)));

	REQUIRE(requests[0].Sent.size() == 1);
	const SentItem& item = requests[0].Sent[0];
	CHECK(item.Kind == SentKind::QueuedPrompt);
	CHECK(item.Label == "look at this [Image #2]");
	CHECK(item.Text == "look at this [Image #2]\n[image/png, 3.0 KB]");
	CHECK(item.Approximate);
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
	CHECK(requests[1].Sent[0].Label == "1 deferred tool name: D, \xe2\x88\x92" "2 removed");
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

TEST_CASE("A tools delta says only what it did, and counts one tool as one")
{
	const std::vector<ApiRequest> requests = Build(
		Fixtures::Attachment({{"type", "deferred_tools_delta"}, {"addedNames", {"A", "B"}}}) +
		Fixtures::Attachment({{"type", "deferred_tools_delta"}, {"addedNames", {"C"}}}) +
		Fixtures::Attachment({{"type", "deferred_tools_delta"}, {"removedNames", {"A", "B"}}}) +
		Fixtures::Attachment({{"type", "deferred_tools_delta"}, {"removedNames", {"C"}}}) +
		Fixtures::Assistant("r1", Fixtures::TextBlock("x"), Fixtures::Usage(1, 1)));

	REQUIRE(requests[0].Sent.size() == 4);
	CHECK(requests[0].Sent[0].Label == "2 deferred tool names: A, B");
	CHECK(requests[0].Sent[1].Label == "1 deferred tool name: C");
	CHECK(requests[0].Sent[2].Label == "\xe2\x88\x92" "2 tools removed");
	CHECK(requests[0].Sent[3].Label == "\xe2\x88\x92" "1 tool removed");
}

TEST_CASE("Tools available name how many of them the request's own deltas loaded")
{
	const std::vector<ApiRequest> requests = Build(
		Fixtures::Attachment({{"type", "deferred_tools_delta"}, {"addedNames", {"A", "B", "C"}}}) +
		Fixtures::Assistant("r1", Fixtures::TextBlock("x"), Fixtures::Usage(1, 1)) +
		Fixtures::Attachment({{"type", "deferred_tools_delta"}, {"addedNames", {"D"}}, {"removedNames", {"A", "B"}}}) +
		Fixtures::Assistant("r2", Fixtures::TextBlock("y"), Fixtures::Usage(1, 1)) +
		Fixtures::UserPrompt("more", "2026-09-17T09:00:05.000Z") +
		Fixtures::Assistant("r3", Fixtures::TextBlock("z"), Fixtures::Usage(1, 1)));

	REQUIRE(requests.size() == 3);
	CHECK(requests[0].ToolsLoaded == 3);
	CHECK(requests[1].ToolsLoaded == 1);
	CHECK(requests[2].ToolsLoaded == 0);
	CHECK(ToolsAvailableLabel(requests[0]) == "Tools: 3 by name only (+3 new)");
	CHECK(ToolsAvailableLabel(requests[1]) == "Tools: 2 by name only (+1 new)");
	CHECK(ToolsAvailableLabel(requests[2]) == "Tools: 2 by name only");
}

TEST_CASE("Tools available label names a snapshot's schemas, and reads \"none recorded\" with neither")
{
	const std::vector<ApiRequest> noSnapshot = Build(Fixtures::Assistant("r1", Fixtures::TextBlock("x"), Fixtures::Usage(1, 1)));

	REQUIRE(noSnapshot.size() == 1);
	CHECK(ToolsAvailableLabel(noSnapshot[0]) == "Tools: none recorded");

	const std::vector<ApiRequest> withSchemas = Build(
		Snapshot(true) +
		Fixtures::UserPrompt("go") +
		Fixtures::Attachment({{"type", "deferred_tools_delta"}, {"addedNames", {"A", "B"}}}) +
		Fixtures::Assistant("r1", Fixtures::TextBlock("x"), Fixtures::Usage(100, 1)));

	REQUIRE(withSchemas.size() == 1);
	CHECK(ToolsAvailableLabel(withSchemas[0]) == "Tools: 1 with schema, 2 by name only (+2 new)");
}

TEST_CASE("A snapshot arriving after the first request gives it a schema count it did not have")
{
	RequestTimeline timeline;
	timeline.Append(ParseTranscript(Fixtures::UserPrompt("go") +
									Fixtures::Assistant("r1", Fixtures::TextBlock("x"), Fixtures::Usage(100, 1)))
						.Records);

	REQUIRE(timeline.GetRequests().size() == 1);
	CHECK(ToolsAvailableLabel(timeline.GetRequests()[0]) == "Tools: none recorded");

	timeline.Append(ParseTranscript(Snapshot(true)).Records);

	CHECK(ToolsAvailableLabel(timeline.GetRequests()[0]) == "Tools: 1 with schema");
}

TEST_CASE("A response's character counts are characters, not bytes")
{
	// "zażółć": six characters, ten bytes in UTF-8.
	const std::vector<ApiRequest> requests = Build(
		Fixtures::Assistant("r1", Fixtures::ThinkingBlock("za\xc5\xbc\xc3\xb3\xc5\x82\xc4\x87"), Fixtures::Usage(1, 1)) +
		Fixtures::Assistant("r1", Fixtures::TextBlock("za\xc5\xbc\xc3\xb3\xc5\x82\xc4\x87"), Fixtures::Usage(1, 1)));

	REQUIRE(requests[0].Response.size() == 2);
	CHECK(requests[0].Response[0].Label == "thinking (6 chars)");
	CHECK(requests[0].Response[1].Label == "6 chars");
}

TEST_CASE("An attachment is measured by what it carries, not by its transcript line")
{
	const std::vector<ApiRequest> requests = Build(
		Fixtures::Attachment({{"type", "total_tokens_reminder"}, {"text", "tokens"}}) +
		Fixtures::Assistant("r1", Fixtures::TextBlock("x"), Fixtures::Usage(1, 1)));

	REQUIRE(requests[0].Sent.size() == 1);
	CHECK(requests[0].Sent[0].Bytes == 6);
}

TEST_CASE("A sent item carries its size in bytes and nothing that guesses at tokens")
{
	// A tool result, an image and an attachment in the one request: every kind a sent item can
	// take size from is measured by its bytes alone. SentItem has no token field to check —
	// this asserts what stands in its place. SentColumn's "≈ " prefix is Panels, not linked
	// into this test binary, so it is not asserted here.
	const std::vector<ApiRequest> requests = Build(
		Fixtures::UserPrompt("go") +
		Fixtures::Assistant("r1", Fixtures::ToolUseBlock("t1", "Read", {{"file_path", "a.cpp"}}), Fixtures::Usage(0, 5, 0, 0, 1000)) +
		Fixtures::ToolResult("t1", "hit") +
		Fixtures::Attachment({{"type", "total_tokens_reminder"}, {"text", "tokens"}}) +
		Fixtures::UserBlocks(nlohmann::json::array({Fixtures::ImageBlock("image/png", std::string(4096, 'A'))}),
							 "2026-09-17T09:00:03.000Z") +
		Fixtures::Assistant("r2", Fixtures::TextBlock("done"), Fixtures::Usage(0, 5, 0, 0, 1100), "claude-opus-5",
							"2026-09-17T09:00:04.000Z"));

	REQUIRE(requests.size() == 2);
	REQUIRE(requests[1].Sent.size() == 3);
	CHECK(requests[1].Sent[0].Kind == SentKind::ToolResult);
	CHECK(requests[1].Sent[0].Bytes == 3);
	CHECK(requests[1].Sent[1].Kind == SentKind::Reminder);
	CHECK(requests[1].Sent[1].Bytes == 6);
	CHECK(requests[1].Sent[2].Kind == SentKind::Image);
	CHECK(requests[1].Sent[2].Bytes == 3072);
}

TEST_CASE("A tool result is measured by the text the model gets, not the metadata kept beside it")
{
	// An Edit's result is a line of text; its toolUseResult carries the whole file, which
	// stays in the transcript and never reaches the model.
	const std::vector<ApiRequest> requests = Build(
		Fixtures::UserPrompt("go") +
		Fixtures::Assistant("r1", Fixtures::ToolUseBlock("t1", "Edit", {{"file_path", "a.cpp"}}), Fixtures::Usage(0, 5, 0, 0, 1000)) +
		Fixtures::ToolResultWith("t1", "ok", {{"originalFile", std::string(5000, 'x')}}) +
		Fixtures::UserPrompt(std::string(200, 'p'), "2026-09-17T09:00:03.000Z") +
		Fixtures::Assistant("r2", Fixtures::TextBlock("done"), Fixtures::Usage(0, 7, 0, 0, 1202), "claude-opus-5",
							"2026-09-17T09:00:09.000Z"));

	REQUIRE(requests.size() == 2);
	REQUIRE(requests[1].Sent.size() == 2);
	CHECK(requests[1].Sent[0].Bytes == 2);
	CHECK(requests[1].Sent[1].Bytes == 200);
}

TEST_CASE("A pasted image is its own sent item, sized by the bytes it decodes to")
{
	const std::vector<ApiRequest> requests = Build(
		Fixtures::Assistant("r1", Fixtures::TextBlock("a"), Fixtures::Usage(0, 1, 0, 0, 1000)) +
		Fixtures::UserBlocks({Fixtures::TextBlock("what is this"), Fixtures::ImageBlock("image/png", std::string(4096, 'A'))},
							 "2026-09-17T09:00:02.000Z") +
		Fixtures::Assistant("r2", Fixtures::TextBlock("b"), Fixtures::Usage(0, 1, 0, 0, 2000), "claude-opus-5",
							"2026-09-17T09:00:05.000Z"));

	REQUIRE(requests.size() == 2);
	REQUIRE(requests[1].Sent.size() == 2);
	CHECK(requests[1].Sent[0].Kind == SentKind::Prompt);
	CHECK(requests[1].Sent[1].Kind == SentKind::Image);
	CHECK(requests[1].Sent[1].Label == "Image (png, 3.0 KB)");
	CHECK(requests[1].Sent[1].Bytes == 3072);
}

TEST_CASE("An image with nothing known about it is still listed")
{
	const std::vector<ApiRequest> requests = Build(
		Fixtures::UserBlocks({{{"type", "image"}}}) +
		Fixtures::Assistant("r1", Fixtures::TextBlock("b"), Fixtures::Usage(1, 1)));

	REQUIRE(requests[0].Sent.size() == 1);
	CHECK(requests[0].Sent[0].Kind == SentKind::Image);
	CHECK(requests[0].Sent[0].Label == "Image");
	CHECK(requests[0].Sent[0].Bytes == 0);
}

TEST_CASE("A synthetic response with an all-zero usage is not the next request's baseline")
{
	// Claude Code writes an interrupt or an API error as a `<synthetic>` response: a uuid, no
	// requestId, and a usage of zeros. Measured against it, the next real request would read
	// as having added its whole context.
	const std::vector<ApiRequest> requests = Build(
		Fixtures::Assistant("r1", Fixtures::TextBlock("a"), Fixtures::Usage(0, 1, 0, 0, 5000)) +
		KeylessAssistant(Fixtures::TextBlock("[Request interrupted by user]"), Fixtures::Usage(0, 0), "2026-09-17T09:00:02.000Z") +
		Fixtures::UserPrompt("again", "2026-09-17T09:00:03.000Z") +
		Fixtures::Assistant("r2", Fixtures::TextBlock("b"), Fixtures::Usage(0, 1, 0, 0, 5200), "claude-opus-5",
							"2026-09-17T09:00:04.000Z"));

	REQUIRE(requests.size() == 3);
	CHECK_FALSE(requests[1].ContextDelta.has_value());
	REQUIRE(requests[2].ContextDelta.has_value());
	CHECK(*requests[2].ContextDelta == 200);
}

TEST_CASE("A tool result for a call the transcript never showed names no tool")
{
	const std::vector<ApiRequest> requests = Build(
		Fixtures::ToolResult("nobody", "x") +
		Fixtures::Assistant("r1", Fixtures::TextBlock("b"), Fixtures::Usage(1, 1)));

	REQUIRE(requests[0].Sent.size() == 1);
	CHECK(requests[0].Sent[0].Kind == SentKind::ToolResult);
	CHECK(requests[0].Sent[0].Label == "unknown tool");
	CHECK(requests[0].Sent[0].ToolName.empty());
}

TEST_CASE("Every sent and returned item carries the text the model saw or wrote")
{
	const std::vector<ApiRequest> requests = Build(
		Fixtures::UserPrompt("go") +
		Fixtures::Assistant("r1", Fixtures::ToolUseBlock("t1", "Bash", {{"command", "ls"}}), Fixtures::Usage(0, 5, 0, 0, 1000)) +
		Fixtures::ToolResult("t1", "a.cpp") +
		// A single-element json initializer list whose element is already a json value collapses to that
		// value rather than an array of one, so the array has to be built explicitly here.
		Fixtures::UserBlocks(nlohmann::json::array({Fixtures::ImageBlock("image/png", std::string(4096, 'A'))}),
							 "2026-09-17T09:00:03.000Z") +
		Fixtures::RenderedAttachment({{"type", "date"}, {"date", "2026-09-24"}}, {"Today is 2026-09-24."}, "2.1.280",
									 "2026-09-17T09:00:04.000Z") +
		Fixtures::Assistant("r2", Fixtures::ThinkingBlock("hmm"), Fixtures::Usage(0, 5, 0, 0, 1100), "claude-opus-5",
							"2026-09-17T09:00:05.000Z") +
		Fixtures::Assistant("r2", Fixtures::TextBlock("done"), Fixtures::Usage(0, 5, 0, 0, 1100), "claude-opus-5",
							"2026-09-17T09:00:05.000Z"));

	REQUIRE(requests.size() == 2);
	REQUIRE(requests[0].Sent.size() == 1);
	CHECK(requests[0].Sent[0].Text == "go");
	REQUIRE(requests[0].Response.size() == 1);
	CHECK(requests[0].Response[0].Text == "Bash\n{\n  \"command\": \"ls\"\n}");

	const ApiRequest& second = requests[1];
	REQUIRE(second.Sent.size() == 3);
	CHECK(second.Sent[0].Text == "a.cpp");
	CHECK(second.Sent[1].Text == "[image/png, 3.0 KB]");
	CHECK(second.Sent[2].Text == "Today is 2026-09-24.");
	CHECK(second.Sent[2].Bytes == second.Sent[2].Text.size());
	CHECK_FALSE(second.Sent[2].Approximate);
	REQUIRE(second.Response.size() == 2);
	CHECK(second.Response[0].Text == "hmm");
	CHECK(second.Response[1].Text == "done");
}

TEST_CASE("An attachment the model never saw is not listed as sent")
{
	const std::vector<ApiRequest> requests = Build(
		Fixtures::VersionedAttachment({{"type", "hook_success"}, {"content", ""}}, "2.1.280") +
		Fixtures::VersionedAttachment({{"type", "deferred_tools_record"}, {"entries", nlohmann::json::array()}}, "2.1.280") +
		Fixtures::VersionedAttachment({{"type", "prompt_snapshot"}}, "2.1.280") +
		Fixtures::UserPrompt("go") +
		Fixtures::Assistant("r1", Fixtures::TextBlock("x"), Fixtures::Usage(1, 1)));

	REQUIRE(requests[0].Sent.size() == 1);
	CHECK(requests[0].Sent[0].Kind == SentKind::Prompt);
}

TEST_CASE("An attachment from before rendered text is listed, marked approximate")
{
	const std::vector<ApiRequest> requests = Build(
		Fixtures::VersionedAttachment({{"type", "total_tokens_reminder"}, {"text", "tokens"}}, "2.1.259") +
		Fixtures::Assistant("r1", Fixtures::TextBlock("x"), Fixtures::Usage(1, 1)));

	REQUIRE(requests[0].Sent.size() == 1);
	CHECK(requests[0].Sent[0].Approximate);
	CHECK(requests[0].Sent[0].Text == "tokens");
	CHECK(requests[0].Sent[0].Bytes == 6);
}

TEST_CASE("An image with nothing known about it has placeholder text")
{
	const std::vector<ApiRequest> requests = Build(
		Fixtures::UserBlocks({{{"type", "image"}}}) +
		Fixtures::Assistant("r1", Fixtures::TextBlock("b"), Fixtures::Usage(1, 1)));

	REQUIRE(requests[0].Sent.size() == 1);
	CHECK(requests[0].Sent[0].Text == "[image]");
}

TEST_CASE("The prompt snapshots put the system prompt and the tool schemas first in the first request")
{
	const std::vector<ApiRequest> requests = Build(
		Snapshot(false) +
		Fixtures::UserPrompt("go") +
		Fixtures::Assistant("r1", Fixtures::TextBlock("x"), Fixtures::Usage(100, 1)) +
		Snapshot(true) +
		Fixtures::UserPrompt("more", "2026-09-17T09:00:05.000Z") +
		Fixtures::Assistant("r2", Fixtures::TextBlock("y"), Fixtures::Usage(150, 1), "claude-opus-5", "2026-09-17T09:00:06.000Z"));

	REQUIRE(requests.size() == 2);
	const ApiRequest& first = requests[0];
	REQUIRE(first.Sent.size() == 3);
	CHECK(first.Sent[0].Kind == SentKind::SystemPrompt);
	CHECK(first.Sent[0].Label == "System prompt");
	// The later snapshot replaced the earlier one rather than adding to it.
	CHECK(first.Sent[0].Text == "Prefix.\n\nBe helpful.");
	CHECK(first.Sent[1].Kind == SentKind::ToolSchemas);
	CHECK(first.Sent[1].Label == "Tools (1 schema)");
	CHECK(first.Sent[1].Bytes == first.Sent[1].Text.size());
	CHECK(first.Sent[2].Kind == SentKind::Prompt);

	// The second snapshot's tool count reaches the first request even though it arrived after
	// it, and every request from then on starts with it.
	CHECK(first.ToolSchemas == 1);

	REQUIRE(requests[1].Sent.size() == 1);
	CHECK(requests[1].Sent[0].Kind == SentKind::Prompt);
	CHECK(requests[1].ToolSchemas == 1);
}

TEST_CASE("A snapshot after the first request changes it and moves the version")
{
	RequestTimeline timeline;
	timeline.Append(ParseTranscript(Fixtures::UserPrompt("go") +
									Fixtures::Assistant("r1", Fixtures::TextBlock("x"), Fixtures::Usage(100, 1)))
						.Records);
	const std::uint64_t before = timeline.GetVersion();

	timeline.Append(ParseTranscript(Snapshot(true)).Records);

	CHECK(timeline.GetVersion() > before);
	REQUIRE(timeline.GetRequests()[0].Sent.size() == 3);
	CHECK(timeline.GetRequests()[0].Sent[0].Kind == SentKind::SystemPrompt);
}

TEST_CASE("A snapshot with no system prompt changes nothing")
{
	const std::vector<ApiRequest> requests = Build(
		Fixtures::VersionedAttachment({{"type", "prompt_snapshot"}, {"tools", nlohmann::json::array({nlohmann::json{{"name", "Read"}}})}},
									  "2.1.280") +
		Fixtures::UserPrompt("go") +
		Fixtures::Assistant("r1", Fixtures::TextBlock("x"), Fixtures::Usage(1, 1)));

	REQUIRE(requests[0].Sent.size() == 1);
	CHECK(requests[0].Sent[0].Kind == SentKind::Prompt);
}

TEST_CASE("The Sent heading says what was added to the context, the same wording for every request")
{
	const std::vector<ApiRequest> requests = Build(
		Fixtures::UserPrompt("go") +
		Fixtures::Assistant("r1", Fixtures::TextBlock("a"), Fixtures::Usage(1, 1)) +
		Fixtures::UserPrompt("again", "2026-09-17T09:00:02.000Z") +
		Fixtures::Assistant("r2", Fixtures::TextBlock("b"), Fixtures::Usage(1, 1), "claude-opus-5",
							"2026-09-17T09:00:03.000Z"));

	REQUIRE(requests.size() == 2);
	CHECK(SentHeading(requests[0]) == "Added to the context");
	CHECK(SentHeading(requests[1]) == "Added to the context");
}

TEST_CASE("The Sent heading says nothing was added to the context when Sent is empty, #1 included")
{
	const std::vector<ApiRequest> requests = Build(
		Fixtures::Assistant("r1", Fixtures::TextBlock("a"), Fixtures::Usage(1, 1)) +
		Fixtures::Assistant("r2", Fixtures::TextBlock("b"), Fixtures::Usage(1, 1), "claude-opus-5",
							"2026-09-17T09:00:02.000Z"));

	REQUIRE(requests.size() == 2);
	CHECK(requests[0].Sent.empty());
	CHECK(requests[1].Sent.empty());
	CHECK(SentHeading(requests[0]) == "Nothing added to the context");
	CHECK(SentHeading(requests[1]) == "Nothing added to the context");
}

TEST_CASE("The Sent summary leads with the request it is new since, then the count and bytes")
{
	const std::vector<ApiRequest> requests = Build(
		Fixtures::UserPrompt("go") +
		Fixtures::Assistant("r1", Fixtures::TextBlock("a"), Fixtures::Usage(1, 1)) +
		Fixtures::UserPrompt("again", "2026-09-17T09:00:02.000Z") +
		Fixtures::Assistant("r2", Fixtures::TextBlock("b"), Fixtures::Usage(1, 1), "claude-opus-5",
							"2026-09-17T09:00:03.000Z"));

	REQUIRE(requests.size() == 2);
	CHECK(SentSummary(requests[0]) == "first request \xc2\xb7 1 item \xc2\xb7 2 B");
	CHECK(SentSummary(requests[1]) == "since #1 \xc2\xb7 1 item \xc2\xb7 5 B");
}

TEST_CASE("The Sent summary is just the reference when Sent is empty, #1 included")
{
	const std::vector<ApiRequest> requests = Build(
		Fixtures::Assistant("r1", Fixtures::TextBlock("a"), Fixtures::Usage(1, 1)) +
		Fixtures::Assistant("r2", Fixtures::TextBlock("b"), Fixtures::Usage(1, 1), "claude-opus-5",
							"2026-09-17T09:00:02.000Z"));

	REQUIRE(requests.size() == 2);
	CHECK(SentSummary(requests[0]) == "first request");
	CHECK(SentSummary(requests[1]) == "since #1");
}

TEST_CASE("The Sent summary adds every item's bytes together")
{
	const std::vector<ApiRequest> requests = Build(
		Fixtures::UserPrompt("go") +
		Fixtures::Assistant("r1", Fixtures::ToolUseBlock("t1", "Read", {{"file_path", "a.cpp"}}), Fixtures::Usage(0, 5, 0, 0, 1000)) +
		Fixtures::ToolResult("t1", std::string(1000, 'x')) +
		Fixtures::Attachment({{"type", "total_tokens_reminder"}, {"text", "tokens"}}) +
		Fixtures::Assistant("r2", Fixtures::TextBlock("done"), Fixtures::Usage(0, 5, 0, 0, 1100), "claude-opus-5",
							"2026-09-17T09:00:04.000Z"));

	REQUIRE(requests.size() == 2);
	REQUIRE(requests[1].Sent.size() == 2);
	CHECK(SentSummary(requests[1]) == "since #1 \xc2\xb7 2 items \xc2\xb7 1006 B");
}

TEST_CASE("The Reply heading says nothing came back when Response is empty")
{
	const std::vector<ApiRequest> requests =
		Build(AssistantStopReasonOnly("r1", "end_turn", "2026-09-17T09:00:01.000Z"));

	REQUIRE(requests.size() == 1);
	CHECK(requests[0].Response.empty());
	CHECK(ReplyHeading(requests[0]) == "Model's reply \xe2\x80\x94 nothing yet");
	CHECK(ReplySummary(requests[0]).empty());
}

TEST_CASE("The Reply summary joins thinking, texts, tool calls and other blocks in that order")
{
	const std::vector<ApiRequest> withTextOnly =
		Build(Fixtures::Assistant("r1", Fixtures::TextBlock("hi"), Fixtures::Usage(1, 1)));

	REQUIRE(withTextOnly.size() == 1);
	CHECK(ReplyHeading(withTextOnly[0]) == "Model's reply");
	CHECK(ReplySummary(withTextOnly[0]) == "text");

	const std::vector<ApiRequest> thinkingAndCall = Build(
		Fixtures::Assistant("r1", Fixtures::ThinkingBlock("hmm"), Fixtures::Usage(0, 1, 0, 0, 100)) +
		Fixtures::Assistant("r1", Fixtures::ToolUseBlock("t1", "Bash", {{"command", "ls"}}), Fixtures::Usage(0, 1, 0, 0, 100)));

	REQUIRE(thinkingAndCall.size() == 1);
	CHECK(ReplySummary(thinkingAndCall[0]) == "thinking \xc2\xb7 1 tool call");

	const std::vector<ApiRequest> severalTextsAndCalls = Build(
		Fixtures::Assistant("r1", Fixtures::ThinkingBlock("hmm"), Fixtures::Usage(0, 1, 0, 0, 100)) +
		Fixtures::Assistant("r1", Fixtures::TextBlock("a"), Fixtures::Usage(0, 1, 0, 0, 100)) +
		Fixtures::Assistant("r1", Fixtures::TextBlock("b"), Fixtures::Usage(0, 1, 0, 0, 100)) +
		Fixtures::Assistant("r1", Fixtures::ToolUseBlock("t1", "Bash", {{"command", "ls"}}), Fixtures::Usage(0, 1, 0, 0, 100)) +
		Fixtures::Assistant("r1", Fixtures::ToolUseBlock("t2", "Bash", {{"command", "pwd"}}), Fixtures::Usage(0, 1, 0, 0, 100)) +
		Fixtures::Assistant("r1", Fixtures::ToolUseBlock("t3", "Bash", {{"command", "id"}}), Fixtures::Usage(0, 1, 0, 0, 100)));

	REQUIRE(severalTextsAndCalls.size() == 1);
	CHECK(ReplySummary(severalTextsAndCalls[0]) == "thinking \xc2\xb7 2 texts \xc2\xb7 3 tool calls");
}

TEST_CASE("The Reply summary names an Other block")
{
	const std::vector<ApiRequest> requests =
		Build(KeylessAssistant({{"type", "redacted_thinking"}}, Fixtures::Usage(1, 1)));

	REQUIRE(requests.size() == 1);
	REQUIRE(requests[0].Response.size() == 1);
	CHECK(requests[0].Response[0].Kind == ResponseKind::Other);
	CHECK(ReplySummary(requests[0]) == "1 other");
}

TEST_CASE("Reset forgets the snapshot too")
{
	RequestTimeline timeline;
	timeline.Append(ParseTranscript(Snapshot(true)).Records);
	timeline.Reset();
	timeline.Append(ParseTranscript(Fixtures::UserPrompt("go") +
									Fixtures::Assistant("r1", Fixtures::TextBlock("x"), Fixtures::Usage(1, 1)))
						.Records);

	REQUIRE(timeline.GetRequests()[0].Sent.size() == 1);
}
