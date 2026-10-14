#include <doctest/doctest.h>

#include "Sessions/Transcript/TranscriptReader.h"

#include "TempDirectory.h"
#include "TranscriptFixtures.h"

#include <chrono>
#include <cstdint>
#include <fstream>
#include <optional>
#include <string>
#include <vector>

using namespace Graphite;

namespace {

	nlohmann::json NestedSchemaTool(const char* name, const char* description)
	{
		const nlohmann::json inputSchema = {{"type", "object"}};

		return nlohmann::json{{"name", name},
							  {"description", description},
							  {"schema",
							   {{"name", name},
								{"description", description},
								{"input_schema", inputSchema},
								{"eager_input_streaming", true}}}};
	}
}

TEST_CASE("Merged response usage takes the larger count in every bucket")
{
	const TokenCounts streamedRecord{5, 100, 7, 1, 40};
	const TokenCounts lastRecord{9, 20, 3, 8, 40};
	const TokenCounts refusalRecord{};

	CHECK(MergeResponseUsage(streamedRecord, lastRecord) == TokenCounts{9, 100, 7, 8, 40});
	CHECK(MergeResponseUsage(lastRecord, streamedRecord) == TokenCounts{9, 100, 7, 8, 40});
	CHECK(MergeResponseUsage(streamedRecord, refusalRecord) == streamedRecord);
}

TEST_CASE("A user prompt and an assistant reply parse into their fields")
{
	const std::string bytes =
		Fixtures::UserPrompt("Fix the build") +
		Fixtures::Assistant("req-1", Fixtures::TextBlock("Done."), Fixtures::Usage(10, 20, 30, 40, 50));

	const TranscriptChunk chunk = ParseTranscript(bytes);

	REQUIRE(chunk.Records.size() == 2);
	CHECK(chunk.Consumed == bytes.size());
	CHECK(chunk.Skipped == 0);

	const TranscriptRecord& user = chunk.Records[0];
	CHECK(user.Kind == RecordKind::User);
	CHECK(user.Cwd == "D:\\Repos\\IDE");
	REQUIRE(user.Blocks.size() == 1);
	CHECK(user.Blocks[0].Kind == BlockKind::Text);
	CHECK(user.Blocks[0].Text == "Fix the build");

	const TranscriptRecord& assistant = chunk.Records[1];
	CHECK(assistant.Kind == RecordKind::Assistant);
	CHECK(assistant.RequestId == "req-1");
	CHECK(assistant.Model == "claude-opus-5");
	REQUIRE(assistant.Usage.has_value());
	CHECK(*assistant.Usage == TokenCounts{10, 20, 30, 40, 50});
	CHECK_FALSE(assistant.Fast);
}

TEST_CASE("A last line without its newline is left for the next read")
{
	const std::string complete = Fixtures::UserPrompt("first");
	std::string partial = Fixtures::UserPrompt("second");
	partial.pop_back();

	const TranscriptChunk chunk = ParseTranscript(complete + partial.substr(0, partial.size() / 2));

	CHECK(chunk.Records.size() == 1);
	CHECK(chunk.Consumed == complete.size());
	CHECK(chunk.Skipped == 0);
}

TEST_CASE("A malformed line is skipped and counted, and the lines after it still read")
{
	const std::string bytes = Fixtures::UserPrompt("before") + "{not json\n" + "\n" +
							  Fixtures::UserPrompt("after");

	const TranscriptChunk chunk = ParseTranscript(bytes);

	CHECK(chunk.Records.size() == 2);
	CHECK(chunk.Skipped == 1);
	CHECK(chunk.Consumed == bytes.size());
}

TEST_CASE("Polish text survives parsing byte for byte")
{
	const std::string text = "Za\xc5\xbc\xc3\xb3\xc5\x82\xc4\x87 g\xc4\x99\xc5\x9bl\xc4\x85 ja\xc5\xba\xc5\x84";

	const TranscriptChunk chunk = ParseTranscript(Fixtures::UserPrompt(text));

	REQUIRE(chunk.Records.size() == 1);
	CHECK(chunk.Records[0].Blocks[0].Text == text);
}

TEST_CASE("Usage without the 5m/1h split counts every cache write as 5m")
{
	nlohmann::json usage = {{"input_tokens", 1},
							{"output_tokens", 2},
							{"cache_creation_input_tokens", 300},
							{"cache_read_input_tokens", 4}};

	const TranscriptChunk chunk =
		ParseTranscript(Fixtures::Assistant("req", Fixtures::TextBlock("x"), usage));

	REQUIRE(chunk.Records[0].Usage.has_value());
	CHECK(chunk.Records[0].Usage->CacheWrite5m == 300);
	CHECK(chunk.Records[0].Usage->CacheWrite1h == 0);
}

TEST_CASE("A fast request is marked as such")
{
	const TranscriptChunk chunk = ParseTranscript(
		Fixtures::Assistant("req", Fixtures::TextBlock("x"), Fixtures::Usage(1, 1, 0, 0, 0, "fast")));

	CHECK(chunk.Records[0].Fast);
}

TEST_CASE("Tool use and tool result blocks carry their ids, name, input and error flag")
{
	constexpr bool resultIsError = true;
	const std::string bytes =
		Fixtures::Assistant("req", Fixtures::ToolUseBlock("toolu_1", "Bash", {{"command", "ls"}}),
							Fixtures::Usage(1, 1)) +
		Fixtures::ToolResult("toolu_1", "boom", resultIsError);

	const TranscriptChunk chunk = ParseTranscript(bytes);

	REQUIRE(chunk.Records.size() == 2);
	const ContentBlock& use = chunk.Records[0].Blocks[0];
	CHECK(use.Kind == BlockKind::ToolUse);
	CHECK(use.ToolUseId == "toolu_1");
	CHECK(use.ToolName == "Bash");
	CHECK(nlohmann::json::parse(use.InputJson) == nlohmann::json{{"command", "ls"}});

	const ContentBlock& result = chunk.Records[1].Blocks[0];
	CHECK(result.Kind == BlockKind::ToolResult);
	CHECK(result.ToolUseId == "toolu_1");
	CHECK(result.Text == "boom");
	CHECK(result.IsError);
}

TEST_CASE("A tool result given as a list of text blocks is joined")
{
	const std::string line = Fixtures::Line(
		{{"type", "user"},
		 {"message",
		  {{"role", "user"},
		   {"content", nlohmann::json::array({{{"type", "tool_result"},
											   {"tool_use_id", "t"},
											   {"content", nlohmann::json::array(
															   {{{"type", "text"}, {"text", "a"}},
																{{"type", "text"}, {"text", "b"}}})}}})}}}});

	const TranscriptChunk chunk = ParseTranscript(line);

	CHECK(chunk.Records[0].Blocks[0].Text == "a\nb");
}

TEST_CASE("Unknown record and block types are kept with their names, not rejected")
{
	const std::string bytes =
		Fixtures::Line({{"type", "permission-mode"}, {"permissionMode", "auto"}}) +
		Fixtures::Assistant("req", {{"type", "server_tool_use"}, {"id", "x"}}, Fixtures::Usage(1, 1));

	const TranscriptChunk chunk = ParseTranscript(bytes);

	REQUIRE(chunk.Records.size() == 2);
	CHECK(chunk.Records[0].Kind == RecordKind::Other);
	CHECK(chunk.Records[0].TypeName == "permission-mode");
	CHECK(chunk.Records[1].Blocks[0].Kind == BlockKind::Other);
	CHECK(chunk.Records[1].Blocks[0].Text == "server_tool_use");
}

TEST_CASE("An ai-title record carries the title and a meta user record says so")
{
	const TranscriptChunk chunk =
		ParseTranscript(Fixtures::AiTitle("Fix the build") + Fixtures::MetaUser("skill text"));

	CHECK(chunk.Records[0].Kind == RecordKind::AiTitle);
	CHECK(chunk.Records[0].Title == "Fix the build");
	CHECK(chunk.Records[1].IsMeta);
}

TEST_CASE("Reading from an offset returns only what was appended since")
{
	TempDirectory temp;
	const std::filesystem::path file = temp.MakeFile("s.jsonl", Fixtures::UserPrompt("one"));

	const Core::Result<TranscriptRead> first = ReadTranscript(file, 0);
	REQUIRE(first.HasValue());
	CHECK(first.GetValue().Chunk.Records.size() == 1);

	{
		std::ofstream stream{file, std::ios::binary | std::ios::app};
		stream << Fixtures::UserPrompt("two");
	}

	const Core::Result<TranscriptRead> second = ReadTranscript(file, first.GetValue().NextOffset);
	REQUIRE(second.HasValue());
	REQUIRE(second.GetValue().Chunk.Records.size() == 1);
	CHECK(second.GetValue().Chunk.Records[0].Blocks[0].Text == "two");
	CHECK_FALSE(second.GetValue().Restarted);
}

TEST_CASE("A tool result keeps the structured result written beside it")
{
	const TranscriptChunk chunk =
		ParseTranscript(Fixtures::ToolResultWith("t", "ok", {{"numLines", 3}, {"numFiles", 2}}));

	REQUIRE(chunk.Records.size() == 1);
	const ContentBlock& block = chunk.Records[0].Blocks[0];
	CHECK(nlohmann::json::parse(block.ResultJson) == nlohmann::json{{"numLines", 3}, {"numFiles", 2}});
}

TEST_CASE("A tool result without a structured result has none")
{
	CHECK(ParseTranscript(Fixtures::ToolResult("t", "ok")).Records[0].Blocks[0].ResultJson.empty());
}

TEST_CASE("A file shorter than the offset is read again from its start")
{
	TempDirectory temp;
	const std::filesystem::path file = temp.MakeFile("s.jsonl", Fixtures::UserPrompt("short"));

	const Core::Result<TranscriptRead> read = ReadTranscript(file, 1'000'000);

	REQUIRE(read.HasValue());
	CHECK(read.GetValue().Restarted);
	CHECK(read.GetValue().Chunk.Records.size() == 1);
}

TEST_CASE("A missing file is an error, not an empty transcript")
{
	TempDirectory temp;

	CHECK_FALSE(ReadTranscript(temp.GetPath() / "none.jsonl", 0).HasValue());
}

TEST_CASE("A file read in small chunks yields the same records as one read")
{
	std::string bytes;
	for (int index = 0; index < 40; ++index)
	{
		bytes += Fixtures::UserPrompt("prompt " + std::to_string(index));
	}

	TempDirectory temp;
	const std::filesystem::path file = temp.MakeFile("s.jsonl", bytes);

	const Core::Result<TranscriptRead> whole = ReadTranscript(file, 0);
	REQUIRE(whole.HasValue());

	std::vector<std::string> chunked;
	std::uint64_t offset = 0;

	for (int reads = 0; offset < bytes.size(); ++reads)
	{
		REQUIRE(reads < 10'000);

		Core::Result<TranscriptRead> read = ReadTranscriptChunk(file, offset, 300);
		REQUIRE(read.HasValue());

		for (const TranscriptRecord& record : read.GetValue().Chunk.Records)
		{
			chunked.push_back(record.Blocks[0].Text);
		}

		REQUIRE(read.GetValue().NextOffset > offset);
		offset = read.GetValue().NextOffset;
	}

	REQUIRE(chunked.size() == whole.GetValue().Chunk.Records.size());
	for (std::size_t index = 0; index < chunked.size(); ++index)
	{
		CHECK(chunked[index] == whole.GetValue().Chunk.Records[index].Blocks[0].Text);
	}
}

TEST_CASE("A chunk that ends in the middle of a line leaves that line for the next chunk")
{
	const std::string first = Fixtures::UserPrompt("first");
	const std::string second = Fixtures::UserPrompt("second");

	TempDirectory temp;
	const std::filesystem::path file = temp.MakeFile("s.jsonl", first + second);

	const Core::Result<TranscriptRead> read = ReadTranscriptChunk(file, 0, first.size() + second.size() / 2);

	REQUIRE(read.HasValue());
	REQUIRE(read.GetValue().Chunk.Records.size() == 1);
	CHECK(read.GetValue().NextOffset == first.size());

	const Core::Result<TranscriptRead> next = ReadTranscriptChunk(file, read.GetValue().NextOffset, 1'000'000);

	REQUIRE(next.HasValue());
	REQUIRE(next.GetValue().Chunk.Records.size() == 1);
	CHECK(next.GetValue().Chunk.Records[0].Blocks[0].Text == "second");
	CHECK(next.GetValue().NextOffset == first.size() + second.size());
}

TEST_CASE("A read reports the size of the file it read")
{
	const std::string bytes = Fixtures::UserPrompt("one") + Fixtures::UserPrompt("two");

	TempDirectory temp;
	const std::filesystem::path file = temp.MakeFile("s.jsonl", bytes);

	const Core::Result<TranscriptRead> chunk = ReadTranscriptChunk(file, 0, 10);
	const Core::Result<TranscriptRead> whole = ReadTranscript(file, 0);

	REQUIRE(chunk.HasValue());
	REQUIRE(whole.HasValue());
	CHECK(chunk.GetValue().FileSize == bytes.size());
	CHECK(whole.GetValue().FileSize == bytes.size());
}

TEST_CASE("A message the user queued while Claude worked keeps its text")
{
	const TranscriptChunk chunk = ParseTranscript(Fixtures::QueuedCommand("also fix the tests"));

	REQUIRE(chunk.Records.size() == 1);
	CHECK(chunk.Records[0].Kind == RecordKind::Attachment);
	CHECK(chunk.Records[0].AttachmentType == "queued_command");
	CHECK(chunk.Records[0].QueuedPrompt == "also fix the tests");
}

TEST_CASE("A queued command the user did not type, or another attachment, has no queued prompt")
{
	const TranscriptChunk chunk = ParseTranscript(
		Fixtures::QueuedCommand("from a hook", "2026-09-17T09:00:03.000Z", "system") +
		Fixtures::QueuedCommand("/compact", "2026-09-17T09:00:04.000Z", "human", "bash") +
		Fixtures::Line({{"type", "attachment"},
						{"timestamp", "2026-09-17T09:00:05.000Z"},
						{"attachment", {{"type", "todo_reminder"}, {"prompt", "not a prompt"}}}}));

	REQUIRE(chunk.Records.size() == 3);
	CHECK(chunk.Records[0].QueuedPrompt.empty());
	CHECK(chunk.Records[1].QueuedPrompt.empty());
	CHECK(chunk.Records[2].QueuedPrompt.empty());
}

TEST_CASE("A queued command with no command mode is taken as a prompt")
{
	const TranscriptChunk chunk = ParseTranscript(Fixtures::Line(
		{{"type", "attachment"},
		 {"timestamp", "2026-09-17T09:00:03.000Z"},
		 {"attachment", {{"type", "queued_command"}, {"prompt", "and this"}, {"origin", {{"kind", "human"}}}}}}));

	REQUIRE(chunk.Records.size() == 1);
	CHECK(chunk.Records[0].QueuedPrompt == "and this");
}

TEST_CASE("A line longer than the chunk is still read whole rather than never")
{
	const std::string longLine = Fixtures::UserPrompt(std::string(5000, 'x'));

	TempDirectory temp;
	const std::filesystem::path file = temp.MakeFile("s.jsonl", longLine + Fixtures::UserPrompt("after"));

	const Core::Result<TranscriptRead> read = ReadTranscriptChunk(file, 0, 100);

	REQUIRE(read.HasValue());
	REQUIRE(read.GetValue().Chunk.Records.size() == 1);
	CHECK(read.GetValue().NextOffset == longLine.size());
}

TEST_CASE("An assistant record keeps why the response stopped")
{
	const TranscriptChunk chunk = ParseTranscript(
		Fixtures::Assistant("r", Fixtures::TextBlock("x"), Fixtures::Usage(1, 1), "claude-opus-5",
							"2026-09-17T09:00:01.000Z", "end_turn"));

	CHECK(chunk.Records[0].StopReason == "end_turn");
}

TEST_CASE("Attachments keep the names, removals and file they carry")
{
	const std::string bytes =
		Fixtures::Attachment({{"type", "deferred_tools_delta"}, {"addedNames", {"WebFetch", "Monitor"}}, {"removedNames", {"Old"}}}) +
		Fixtures::Attachment({{"type", "skill_listing"}, {"names", {"a", "b", "c"}}, {"skillCount", 3}}) +
		Fixtures::Attachment({{"type", "mcp_instructions_delta"}, {"addedNames", {"ghidra"}}}) +
		Fixtures::Attachment({{"type", "edited_text_file"}, {"filename", "D:\\a.cpp"}}) +
		Fixtures::Attachment({{"type", "deferred_tools_delta"}, {"addedNames", "not a list"}});

	const TranscriptChunk chunk = ParseTranscript(bytes);

	REQUIRE(chunk.Records.size() == 5);
	CHECK(chunk.Records[0].AttachmentNames == std::vector<std::string>{"WebFetch", "Monitor"});
	CHECK(chunk.Records[0].AttachmentRemoved == std::vector<std::string>{"Old"});
	CHECK(chunk.Records[1].AttachmentNames == std::vector<std::string>{"a", "b", "c"});
	CHECK(chunk.Records[2].AttachmentNames == std::vector<std::string>{"ghidra"});
	CHECK(chunk.Records[3].AttachmentFile == "D:\\a.cpp");
	CHECK(chunk.Records[4].AttachmentNames.empty());
}

TEST_CASE("Every record knows how many bytes its line took")
{
	const std::string line = Fixtures::UserPrompt("hello");
	CHECK(ParseTranscript(line).Records[0].Bytes == line.size() - 1);
}

TEST_CASE("An attachment with no rendered text is reconstructed from what it carries, and says so")
{
	const std::string bytes =
		Fixtures::Attachment({{"type", "total_tokens_reminder"}, {"text", "12345"}}) +
		Fixtures::Attachment({{"type", "skill_listing"}, {"content", "- a: does a"}, {"names", {"a"}}}) +
		Fixtures::Attachment({{"type", "deferred_tools_delta"}, {"addedNames", {"A", "B"}}, {"addedLines", {"A: a", "B: bb"}}}) +
		Fixtures::Attachment({{"type", "mcp_instructions_delta"}, {"addedNames", {"x"}}, {"addedBlocks", {"## x\nuse x"}}}) +
		Fixtures::Attachment({{"type", "edited_text_file"}, {"filename", "D:\a.cpp"}, {"snippet", "int a;"}}) +
		Fixtures::QueuedCommand("also this") +
		Fixtures::Attachment({{"type", "date"}, {"date", "2026-09-24"}});

	const TranscriptChunk chunk = ParseTranscript(bytes);

	REQUIRE(chunk.Records.size() == 7);
	CHECK(chunk.Records[0].AttachmentText == "12345");
	CHECK(chunk.Records[1].AttachmentText == "- a: does a");
	CHECK(chunk.Records[2].AttachmentText == "A: a\nB: bb");
	CHECK(chunk.Records[3].AttachmentText == "## x\nuse x");
	CHECK(chunk.Records[4].AttachmentText == "int a;");
	CHECK(chunk.Records[5].AttachmentText == "also this");

	CHECK(chunk.Records[6].AttachmentText == nlohmann::json({{"type", "date"}, {"date", "2026-09-24"}}).dump());
	CHECK(chunk.Records[6].AttachmentText.size() < chunk.Records[6].Bytes);

	for (const TranscriptRecord& record : chunk.Records)
	{
		CHECK(record.AttachmentSent);
		CHECK(record.AttachmentApproximate);
	}
}

TEST_CASE("An attachment's rendered text is what it sent, joined a block to a line")
{
	const TranscriptChunk chunk = ParseTranscript(Fixtures::RenderedAttachment(
		{{"type", "date"}, {"date", "2026-09-24"}},
		{"<system-reminder>\nToday's date is 2026-09-24.\n</system-reminder>", "second"}));

	REQUIRE(chunk.Records.size() == 1);
	CHECK(chunk.Records[0].AttachmentText == "<system-reminder>\nToday's date is 2026-09-24.\n</system-reminder>\nsecond");
	CHECK(chunk.Records[0].AttachmentSent);
	CHECK_FALSE(chunk.Records[0].AttachmentApproximate);
}

TEST_CASE("An empty rendered list is an attachment that sent nothing, not one that was not sent")
{
	const TranscriptChunk chunk = ParseTranscript(Fixtures::RenderedAttachment({{"type", "date"}, {"date", "x"}}, {}));

	REQUIRE(chunk.Records.size() == 1);
	CHECK(chunk.Records[0].AttachmentText.empty());
	CHECK(chunk.Records[0].AttachmentSent);
	CHECK_FALSE(chunk.Records[0].AttachmentApproximate);
}

TEST_CASE("Rendered elements of the wrong shape contribute nothing and cost nothing else")
{
	const std::string line = Fixtures::Line(
		{{"type", "attachment"},
		 {"version", "2.1.280"},
		 {"attachment", {{"type", "date"}}},
		 {"rendered", nlohmann::json::array({42, nlohmann::json{{"content", 7}}, nlohmann::json{{"content", "kept"}}})}});

	const TranscriptChunk chunk = ParseTranscript(line);

	REQUIRE(chunk.Records.size() == 1);
	CHECK(chunk.Records[0].AttachmentText == "kept");
	CHECK(chunk.Records[0].AttachmentSent);
}

TEST_CASE("From 2.1.260 on, an attachment without rendered text was never sent")
{
	const TranscriptChunk chunk = ParseTranscript(
		Fixtures::VersionedAttachment({{"type", "hook_success"}, {"content", ""}}, "2.1.260") +
		Fixtures::VersionedAttachment({{"type", "hook_success"}, {"content", ""}}, "2.2.0") +
		Fixtures::VersionedAttachment({{"type", "total_tokens_reminder"}, {"text", "old"}}, "2.1.259") +
		Fixtures::VersionedAttachment({{"type", "total_tokens_reminder"}, {"text", "odd"}}, "not a version"));

	REQUIRE(chunk.Records.size() == 4);
	CHECK_FALSE(chunk.Records[0].AttachmentSent);
	CHECK(chunk.Records[0].AttachmentText.empty());
	CHECK_FALSE(chunk.Records[1].AttachmentSent);

	CHECK(chunk.Records[2].AttachmentSent);
	CHECK(chunk.Records[2].AttachmentApproximate);
	CHECK(chunk.Records[2].AttachmentText == "old");
	CHECK(chunk.Records[3].AttachmentSent);
	CHECK(chunk.Records[3].AttachmentApproximate);
	CHECK(chunk.Records[3].AttachmentText == "odd");
}

TEST_CASE("Each record of a transcript that changed version is judged by its own version")
{
	const TranscriptChunk chunk = ParseTranscript(
		Fixtures::VersionedAttachment({{"type", "total_tokens_reminder"}, {"text", "before"}}, "2.1.251") +
		Fixtures::VersionedAttachment({{"type", "hook_success"}, {"content", ""}}, "2.1.280") +
		Fixtures::RenderedAttachment({{"type", "total_tokens_reminder"}, {"text", "x"}}, {"after"}, "2.1.280"));

	REQUIRE(chunk.Records.size() == 3);
	CHECK(chunk.Records[0].AttachmentSent);
	CHECK(chunk.Records[0].AttachmentApproximate);
	CHECK_FALSE(chunk.Records[1].AttachmentSent);
	CHECK(chunk.Records[2].AttachmentText == "after");
	CHECK_FALSE(chunk.Records[2].AttachmentApproximate);
}

TEST_CASE("An image block keeps its media type and data length, not the data")
{
	const std::string data(4000, 'A');
	const TranscriptChunk chunk = ParseTranscript(
		Fixtures::UserBlocks({Fixtures::TextBlock("look"), Fixtures::ImageBlock("image/png", data)}));

	REQUIRE(chunk.Records.size() == 1);
	REQUIRE(chunk.Records[0].Blocks.size() == 2);
	const ContentBlock& image = chunk.Records[0].Blocks[1];
	CHECK(image.Kind == BlockKind::Image);
	CHECK(image.Text == "image/png");
	CHECK(image.DataBytes == 4000);
}

TEST_CASE("An image block with a missing or mistyped source is still an image, with nothing known about it")
{
	const TranscriptChunk chunk = ParseTranscript(Fixtures::UserBlocks(
		{{{"type", "image"}},
		 {{"type", "image"}, {"source", "not an object"}},
		 {{"type", "image"}, {"source", {{"media_type", 7}, {"data", {1, 2}}}}}}));

	REQUIRE(chunk.Records.size() == 1);
	REQUIRE(chunk.Records[0].Blocks.size() == 3);

	for (const ContentBlock& block : chunk.Records[0].Blocks)
	{
		CHECK(block.Kind == BlockKind::Image);
		CHECK(block.Text.empty());
		CHECK(block.DataBytes == 0);
	}
}

TEST_CASE("A prompt snapshot carries the system prompt and the tool schemas as text")
{
	const std::string bytes = Fixtures::VersionedAttachment(
		{{"type", "prompt_snapshot"},
		 {"cliPrefix", "You are Claude Code."},
		 {"systemPrompt", nlohmann::json::array({"First.", "Second."})},
		 {"tools", nlohmann::json::array({NestedSchemaTool("Read", "Reads a file."), NestedSchemaTool("Bash", "Runs.")})}},
		"2.1.280");

	const TranscriptChunk chunk = ParseTranscript(bytes);

	REQUIRE(chunk.Records.size() == 1);
	const TranscriptRecord& record = chunk.Records[0];
	CHECK(record.SystemPromptText == "You are Claude Code.\n\nFirst.\n\nSecond.");
	CHECK(record.ToolSchemaCount == 2);

	const std::string rule = "\n\n" + std::string(40, '-') + "\n\n";
	CHECK(record.ToolSchemasText == "Read\n\nReads a file.\n\n{\n  \"type\": \"object\"\n}" + rule +
										"Bash\n\nRuns.\n\n{\n  \"type\": \"object\"\n}");

	CHECK_FALSE(record.AttachmentSent);
}

TEST_CASE("A prompt snapshot without tools, or without a system prompt, says only what it has")
{
	const TranscriptChunk chunk = ParseTranscript(
		Fixtures::VersionedAttachment({{"type", "prompt_snapshot"}, {"systemPrompt", nlohmann::json::array({"Only."})}}, "2.1.280") +
		Fixtures::VersionedAttachment(
			{{"type", "prompt_snapshot"},
			 {"cliPrefix", "Prefix."},
			 {"tools", nlohmann::json::array({nlohmann::json{{"name", "Read"}}, "not a tool"})}},
			"2.1.280"));

	REQUIRE(chunk.Records.size() == 2);
	CHECK(chunk.Records[0].SystemPromptText == "Only.");
	CHECK(chunk.Records[0].ToolSchemaCount == 0);
	CHECK(chunk.Records[0].ToolSchemasText.empty());

	CHECK(chunk.Records[1].SystemPromptText.empty());
	CHECK(chunk.Records[1].ToolSchemaCount == 1);
	CHECK(chunk.Records[1].ToolSchemasText == "Read");
}

TEST_CASE("A tool with its input schema at the top rather than nested is read all the same")
{
	const TranscriptChunk chunk = ParseTranscript(Fixtures::VersionedAttachment(
		{{"type", "prompt_snapshot"},
		 {"systemPrompt", nlohmann::json::array({"Only."})},
		 {"tools", nlohmann::json::array({nlohmann::json{{"name", "Read"}, {"input_schema", {{"type", "object"}}}}})}},
		"2.1.280"));

	REQUIRE(chunk.Records.size() == 1);
	CHECK(chunk.Records[0].ToolSchemasText == "Read\n\n{\n  \"type\": \"object\"\n}");
}

TEST_CASE("The system prompt leaves out the marker Claude Code splits its cache at")
{
	const TranscriptChunk chunk = ParseTranscript(Fixtures::VersionedAttachment(
		{{"type", "prompt_snapshot"},
		 {"systemPrompt",
		  nlohmann::json::array({"First.", "__SYSTEM_PROMPT_DYNAMIC_BOUNDARY__", "Second.", " __SYSTEM_PROMPT_DYNAMIC_BOUNDARY__\n",
								 "Mentions __SYSTEM_PROMPT_DYNAMIC_BOUNDARY__ in passing."})}},
		"2.1.280"));

	REQUIRE(chunk.Records.size() == 1);
	CHECK(chunk.Records[0].SystemPromptText ==
		  "First.\n\nSecond.\n\nMentions __SYSTEM_PROMPT_DYNAMIC_BOUNDARY__ in passing.");
}

TEST_CASE("A system prompt that is only the marker is no system prompt")
{
	const TranscriptChunk chunk = ParseTranscript(Fixtures::VersionedAttachment(
		{{"type", "prompt_snapshot"},
		 {"cliPrefix", "Prefix."},
		 {"systemPrompt", nlohmann::json::array({"__SYSTEM_PROMPT_DYNAMIC_BOUNDARY__"})}},
		"2.1.280"));

	REQUIRE(chunk.Records.size() == 1);
	CHECK(chunk.Records[0].SystemPromptText.empty());
}

TEST_CASE("A queued prompt without rendered text was still sent, and is reconstructed")
{
	const TranscriptChunk chunk = ParseTranscript(Fixtures::VersionedAttachment(
		{{"type", "queued_command"}, {"prompt", "also this"}, {"origin", {{"kind", "human"}}}, {"commandMode", "prompt"}},
		"2.1.280"));

	REQUIRE(chunk.Records.size() == 1);
	CHECK(chunk.Records[0].AttachmentSent);
	CHECK(chunk.Records[0].AttachmentApproximate);
	CHECK(chunk.Records[0].AttachmentText == "also this");
	CHECK(chunk.Records[0].QueuedPrompt == "also this");
}

TEST_CASE("A queued prompt given as blocks is its text with a placeholder for each image, not the image's data")
{
	const nlohmann::json blocks = nlohmann::json::array(
		{Fixtures::TextBlock("look at this [Image #2]"), Fixtures::ImageBlock("image/png", std::string(4096, 'A'))});
	const nlohmann::json imageOnly = nlohmann::json::array({Fixtures::ImageBlock("image/png", std::string(4096, 'A'))});

	const TranscriptChunk chunk = ParseTranscript(
		Fixtures::VersionedAttachment(
			{{"type", "queued_command"}, {"prompt", blocks}, {"origin", {{"kind", "human"}}}, {"commandMode", "prompt"}},
			"2.1.261") +
		Fixtures::VersionedAttachment(
			{{"type", "queued_command"}, {"prompt", imageOnly}, {"origin", {{"kind", "human"}}}, {"commandMode", "prompt"}},
			"2.1.261"));

	REQUIRE(chunk.Records.size() == 2);
	CHECK(chunk.Records[0].AttachmentSent);
	CHECK(chunk.Records[0].AttachmentApproximate);
	CHECK(chunk.Records[0].AttachmentText == "look at this [Image #2]\n[image/png, 3.0 KB]");
	CHECK(chunk.Records[0].QueuedPrompt == "look at this [Image #2]\n[image/png, 3.0 KB]");

	CHECK(chunk.Records[1].AttachmentText == "[image/png, 3.0 KB]");
	CHECK(chunk.Records[1].QueuedPrompt == "[image/png, 3.0 KB]");
}

TEST_CASE("Only a prompt snapshot fills the snapshot fields")
{
	const TranscriptChunk chunk = ParseTranscript(Fixtures::VersionedAttachment(
		{{"type", "date"}, {"systemPrompt", nlohmann::json::array({"no"})}}, "2.1.259"));

	REQUIRE(chunk.Records.size() == 1);
	CHECK(chunk.Records[0].SystemPromptText.empty());
}

TEST_CASE("A compact boundary reads its trigger, token counts and duration")
{
	const std::vector<TranscriptRecord> records = ParseTranscript(Fixtures::kCompactBoundary).Records;

	REQUIRE(records.size() == 1);
	const TranscriptRecord& record = records[0];
	CHECK(record.Kind == RecordKind::System);
	CHECK(record.IsCompactBoundary);
	CHECK(record.Timestamp == "2026-08-31T16:33:10.464Z");
	CHECK(record.Compaction.Trigger == "auto");
	CHECK(record.Compaction.PreTokens == std::optional<std::uint64_t>{167324});
	CHECK(record.Compaction.PostTokens == std::optional<std::uint64_t>{5090});
	CHECK(record.Compaction.Duration == std::optional<std::chrono::milliseconds>{169810});
}

TEST_CASE("A compact boundary with no metadata reads with every fact absent")
{
	const std::vector<TranscriptRecord> records = ParseTranscript(
													  Fixtures::Line({{"type", "system"}, {"subtype", "compact_boundary"}, {"timestamp", "2026-08-31T16:33:10.464Z"}}))
													  .Records;

	REQUIRE(records.size() == 1);
	CHECK(records[0].IsCompactBoundary);
	CHECK(records[0].Compaction.Trigger.empty());
	CHECK_FALSE(records[0].Compaction.PreTokens.has_value());
	CHECK_FALSE(records[0].Compaction.PostTokens.has_value());
	CHECK_FALSE(records[0].Compaction.Duration.has_value());
}

TEST_CASE("A compact boundary's count of the wrong type reads as absent, the rest still read")
{
	const std::vector<TranscriptRecord> records = ParseTranscript(Fixtures::Line(
																	  {{"type", "system"},
																	   {"subtype", "compact_boundary"},
																	   {"compactMetadata", {{"trigger", "manual"}, {"preTokens", "lots"}, {"postTokens", 4000}, {"durationMs", -5}}}}))
													  .Records;

	REQUIRE(records.size() == 1);
	CHECK(records[0].Compaction.Trigger == "manual");
	CHECK_FALSE(records[0].Compaction.PreTokens.has_value());
	CHECK(records[0].Compaction.PostTokens == std::optional<std::uint64_t>{4000});
	CHECK_FALSE(records[0].Compaction.Duration.has_value());
}

TEST_CASE("Another system record is not a compact boundary")
{
	const std::vector<TranscriptRecord> records =
		ParseTranscript(Fixtures::Line({{"type", "system"}, {"subtype", "turn_duration"}, {"durationMs", 5000}})).Records;

	REQUIRE(records.size() == 1);
	CHECK_FALSE(records[0].IsCompactBoundary);
}
