#include <doctest/doctest.h>

#include "Sessions/Session.h"
#include "Sessions/SessionSummary.h"
#include "Sessions/Transcript/SubagentLinks.h"
#include "Sessions/Transcript/TranscriptReader.h"

#include "TempDirectory.h"

#include <filesystem>
#include <optional>
#include <set>
#include <string>
#include <utility>

using namespace Graphite;

namespace {
	const std::string kMetaFileWrittenByClaudeCode2_1_251 =
		R"({"agentType":"general-purpose","description":"Review Task 1","toolUseId":"toolu_01CQ2NJ6fwiXfNPqiyaMCc8f","spawnDepth":1,"model":"sonnet"})";
}

TEST_CASE("A subagent's meta file names the call that started it")
{
	const std::optional<std::pair<std::string, SubagentInfo>> link = ParseSubagentMeta(kMetaFileWrittenByClaudeCode2_1_251, "ac9666ea1479fd4d4");

	REQUIRE(link.has_value());
	const auto& [toolUseId, info] = *link;
	CHECK(toolUseId == "toolu_01CQ2NJ6fwiXfNPqiyaMCc8f");
	CHECK(info.AgentId == "ac9666ea1479fd4d4");
	CHECK(info.AgentType == "general-purpose");
	CHECK(info.Description == "Review Task 1");
}

TEST_CASE("A meta file without the fields it describes the agent by still links it")
{
	const std::optional<std::pair<std::string, SubagentInfo>> link = ParseSubagentMeta(R"({"toolUseId":"toolu_1"})", "a1");

	REQUIRE(link.has_value());
	const auto& [toolUseId, info] = *link;
	CHECK(toolUseId == "toolu_1");
	CHECK(info.AgentType.empty());
	CHECK(info.Description.empty());
}

TEST_CASE("A meta file that names no call links nothing")
{
	CHECK_FALSE(ParseSubagentMeta(R"({"agentType":"general-purpose"})", "a1").has_value());
	CHECK_FALSE(ParseSubagentMeta(R"({"toolUseId":42})", "a1").has_value());
	CHECK_FALSE(ParseSubagentMeta(R"({"toolUseId":""})", "a1").has_value());
	CHECK_FALSE(ParseSubagentMeta(R"(["toolUseId"])", "a1").has_value());
	CHECK_FALSE(ParseSubagentMeta(R"({"toolUseId":"toolu_1")", "a1").has_value());
	CHECK_FALSE(ParseSubagentMeta("", "a1").has_value());
}

TEST_CASE("A subagent's meta file is read from beside its transcript")
{
	TempDirectory temp;
	const std::filesystem::path transcript = temp.MakeFile("p/s/subagents/agent-a1.jsonl");
	temp.MakeFile("p/s/subagents/agent-a1.meta.json", kMetaFileWrittenByClaudeCode2_1_251);

	const std::optional<std::pair<std::string, SubagentInfo>> link = ReadSubagentMeta(transcript);

	REQUIRE(link.has_value());
	const auto& [toolUseId, info] = *link;
	CHECK(toolUseId == "toolu_01CQ2NJ6fwiXfNPqiyaMCc8f");
	CHECK(info.AgentId == "a1");

	CHECK_FALSE(ReadSubagentMeta(temp.MakeFile("p/s/subagents/agent-a2.jsonl")).has_value());
}

TEST_CASE("A subagent's transcript lies in its session's subagent folder")
{
	const std::filesystem::path session = std::filesystem::path{"p"} / "s.jsonl";

	CHECK(SubagentTranscriptPath(session, "a1") == SubagentDirectory(session) / "agent-a1.jsonl");
}

TEST_CASE("A subagent's transcript reads as a session of its own")
{
	const std::string realSubagentTranscript =
		R"({"parentUuid":null,"isSidechain":true,"promptId":"2cc28b71-f036-4e88-9337-1bef264ccce0","agentId":"ac9666ea1479fd4d4","type":"user","message":{"role":"user","content":"You are reviewing one completed task."},"uuid":"e1b112ee-9a59-43a4-9705-286a2503b963","timestamp":"2026-08-29T14:49:52.238Z","userType":"external","entrypoint":"cli","cwd":"D:\\Repos\\IDE","sessionId":"17eb9736-9ed9-4b84-9c52-6ed62e9fa11a","version":"2.1.251","gitBranch":"main"})"
		"\n"
		R"({"parentUuid":"51b5f0b5-7a76-43b6-88e0-561c0f03aa94","isSidechain":true,"agentId":"ac9666ea1479fd4d4","message":{"model":"claude-sonnet-5","id":"msg_011CeXE3fkEd3JEpP8VNw4Ja","type":"message","role":"assistant","content":[{"type":"tool_use","id":"toolu_01AVqVz1AcJ2gEPQe3sWtZMH","name":"Read","input":{"file_path":"D:\\Repos\\IDE\\plan.md"},"caller":{"type":"direct"}}],"stop_reason":null,"stop_sequence":null,"stop_details":null,"usage":{"input_tokens":2,"cache_creation_input_tokens":9091,"cache_read_input_tokens":27300,"cache_creation":{"ephemeral_5m_input_tokens":9091,"ephemeral_1h_input_tokens":0},"output_tokens":5,"service_tier":"standard","inference_geo":"not_available"},"diagnostics":null},"requestId":"req_011CeXE3ezqSkgFaGhts9bCq","attributionAgent":"general-purpose","type":"assistant","uuid":"66f5e979-3cfd-4508-80c9-268afbc78dc0","timestamp":"2026-08-29T14:49:56.219Z","effort":"medium","userType":"external","entrypoint":"cli","cwd":"D:\\Repos\\IDE","sessionId":"17eb9736-9ed9-4b84-9c52-6ed62e9fa11a","version":"2.1.251","gitBranch":"main"})"
		"\n"
		R"({"parentUuid":"66f5e979-3cfd-4508-80c9-268afbc78dc0","isSidechain":true,"promptId":"2cc28b71-f036-4e88-9337-1bef264ccce0","agentId":"ac9666ea1479fd4d4","type":"user","message":{"role":"user","content":[{"tool_use_id":"toolu_01AVqVz1AcJ2gEPQe3sWtZMH","type":"tool_result","content":"1\t### Task 1"}]},"uuid":"df3bd49c-021e-4355-8582-a3f877cf098f","timestamp":"2026-08-29T14:49:56.229Z","toolUseResult":{"type":"text","file":{"filePath":"D:\\Repos\\IDE\\plan.md","content":"1\t### Task 1","numLines":1,"startLine":1,"totalLines":1}},"sourceToolAssistantUUID":"66f5e979-3cfd-4508-80c9-268afbc78dc0","userType":"external","entrypoint":"cli","cwd":"D:\\Repos\\IDE","sessionId":"17eb9736-9ed9-4b84-9c52-6ed62e9fa11a","version":"2.1.251","gitBranch":"main"})"
		"\n"
		R"({"parentUuid":"1ce20c4f-7ad8-4f37-9209-8a54ef4b372d","isSidechain":true,"agentId":"ac9666ea1479fd4d4","message":{"model":"claude-sonnet-5","id":"msg_011CeXE53RjvQmBzVUE1uy6D","type":"message","role":"assistant","content":[{"type":"text","text":"## Verdict\n\nSpec compliance: yes."}],"stop_reason":null,"stop_sequence":null,"stop_details":null,"usage":{"input_tokens":2,"cache_creation_input_tokens":1439,"cache_read_input_tokens":47626,"cache_creation":{"ephemeral_5m_input_tokens":1439,"ephemeral_1h_input_tokens":0},"output_tokens":3,"service_tier":"standard","inference_geo":"not_available"},"diagnostics":null},"requestId":"req_011CeXE52dNQnFBEtmf1Gvc2","attributionAgent":"general-purpose","type":"assistant","uuid":"98ed36e5-5ca3-4a76-b387-2edbcd2cdcb6","timestamp":"2026-08-29T14:50:24.508Z","effort":"medium","userType":"external","entrypoint":"cli","cwd":"D:\\Repos\\IDE","sessionId":"17eb9736-9ed9-4b84-9c52-6ed62e9fa11a","version":"2.1.251","gitBranch":"main"})"
		"\n";

	Session session;
	session.Append(ParseTranscript(realSubagentTranscript).Records);

	REQUIRE(session.GetTurns().size() == 1);
	const Turn& turn = session.GetTurns().front();
	CHECK(turn.Prompt == "You are reviewing one completed task.");

	REQUIRE(session.GetTools().size() == 1);
	CHECK(session.GetTools().front().Name == "Read");
	CHECK(session.GetTools().front().HasResult);

	REQUIRE_FALSE(turn.Items.empty());
	CHECK(turn.Items.back().Kind == ItemKind::Text);
	CHECK(turn.Items.back().Text == "## Verdict\n\nSpec compliance: yes.");
}

TEST_CASE("A subagent's id is what follows agent- in its transcript's name, and any other name has none")
{
	CHECK(SubagentIdOf("C:/p/s/subagents/agent-a1b2.jsonl") == std::optional<std::string>{"a1b2"});
	CHECK_FALSE(SubagentIdOf("C:/p/s/subagents/notes.jsonl").has_value());
}

TEST_CASE("Every agent- transcript of a session is visited with its id, and no other file")
{
	TempDirectory temp;
	const std::filesystem::path session = temp.MakeFile("p/s.jsonl");
	temp.MakeFile("p/s/subagents/agent-a1.jsonl");
	temp.MakeFile("p/s/subagents/agent-b2.jsonl");
	temp.MakeFile("p/s/subagents/agent-a1.meta.json", "{}");
	temp.MakeFile("p/s/subagents/notes.jsonl");

	std::set<std::string> ids;
	const bool sawWholeFolder =
		ForEachSubagentTranscript(session, [&ids](const std::filesystem::path&, const std::string& agentId) { ids.insert(agentId); });

	CHECK(sawWholeFolder);
	CHECK(ids == std::set<std::string>{"a1", "b2"});
}

TEST_CASE("A session without a subagents folder has no subagent transcripts, and the walk saw it whole")
{
	TempDirectory temp;
	const std::filesystem::path session = temp.MakeFile("p/s.jsonl");
	bool visited = false;

	const bool sawWholeFolder =
		ForEachSubagentTranscript(session, [&visited](const std::filesystem::path&, const std::string&) { visited = true; });

	CHECK(sawWholeFolder);
	CHECK_FALSE(visited);
}