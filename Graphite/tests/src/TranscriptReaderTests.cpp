#include <doctest/doctest.h>

#include "Sessions/TranscriptReader.h"

#include "TempDirectory.h"
#include "TranscriptFixtures.h"

#include <cstdint>
#include <fstream>
#include <string>
#include <vector>

using namespace Graphite;

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
	const std::string bytes =
		Fixtures::Assistant("req", Fixtures::ToolUseBlock("toolu_1", "Bash", {{"command", "ls"}}),
							Fixtures::Usage(1, 1)) +
		Fixtures::ToolResult("toolu_1", "boom", true);

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
		bytes += Fixtures::UserPrompt("prompt " + std::to_string(index));

	TempDirectory temp;
	const std::filesystem::path file = temp.MakeFile("s.jsonl", bytes);

	const Core::Result<TranscriptRead> whole = ReadTranscript(file, 0);
	REQUIRE(whole.HasValue());

	std::vector<std::string> chunked;
	std::uint64_t offset = 0;

	// Small enough that most chunks end in the middle of a line.
	for (int reads = 0; offset < bytes.size(); ++reads)
	{
		REQUIRE(reads < 10'000);

		Core::Result<TranscriptRead> read = ReadTranscriptChunk(file, offset, 300);
		REQUIRE(read.HasValue());

		for (const TranscriptRecord& record : read.GetValue().Chunk.Records)
			chunked.push_back(record.Blocks[0].Text);

		// A line longer than the limit would never be consumed; the chunk reader must not
		// stall on one.
		REQUIRE(read.GetValue().NextOffset > offset);
		offset = read.GetValue().NextOffset;
	}

	REQUIRE(chunked.size() == whole.GetValue().Chunk.Records.size());
	for (std::size_t index = 0; index < chunked.size(); ++index)
		CHECK(chunked[index] == whole.GetValue().Chunk.Records[index].Blocks[0].Text);
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
