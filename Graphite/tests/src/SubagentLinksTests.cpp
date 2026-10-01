#include <doctest/doctest.h>

#include "Sessions/Session.h"
#include "Sessions/SessionSummary.h"
#include "Sessions/SubagentLinks.h"
#include "Sessions/TranscriptReader.h"

#include "TempDirectory.h"

#include <string>

using namespace Graphite;

namespace {
	// A real meta file, as Claude Code 2.1.251 wrote it.
	const std::string kMeta =
		R"({"agentType":"general-purpose","description":"Review Task 1","toolUseId":"toolu_01CQ2NJ6fwiXfNPqiyaMCc8f","spawnDepth":1,"model":"sonnet"})";
}

TEST_CASE("A subagent's meta file names the call that started it")
{
	const auto link = ParseSubagentMeta(kMeta, "ac9666ea1479fd4d4");

	REQUIRE(link.has_value());
	CHECK(link->first == "toolu_01CQ2NJ6fwiXfNPqiyaMCc8f");
	CHECK(link->second.AgentId == "ac9666ea1479fd4d4");
	CHECK(link->second.AgentType == "general-purpose");
	CHECK(link->second.Description == "Review Task 1");
}

TEST_CASE("A meta file without the fields it describes the agent by still links it")
{
	const auto link = ParseSubagentMeta(R"({"toolUseId":"toolu_1"})", "a1");

	REQUIRE(link.has_value());
	CHECK(link->first == "toolu_1");
	CHECK(link->second.AgentType.empty());
	CHECK(link->second.Description.empty());
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
	temp.MakeFile("p/s/subagents/agent-a1.meta.json", kMeta);

	const auto link = ReadSubagentMeta(transcript);

	REQUIRE(link.has_value());
	CHECK(link->first == "toolu_01CQ2NJ6fwiXfNPqiyaMCc8f");
	CHECK(link->second.AgentId == "a1");

	CHECK_FALSE(ReadSubagentMeta(temp.MakeFile("p/s/subagents/agent-a2.jsonl")).has_value());
}

TEST_CASE("A subagent's transcript lies in its session's subagent folder")
{
	const std::filesystem::path session = std::filesystem::path{"p"} / "s.jsonl";

	CHECK(SubagentTranscriptPath(session, "a1") == SubagentDirectory(session) / "agent-a1.jsonl");
}

TEST_CASE("A subagent's transcript reads as a session of its own")
{
	// Four lines of a real subagent transcript (2.1.251), their text cut short: the prompt
	// as a string, a Read call, its result, and the final reply. Every record is a sidechain.
	const std::string transcript =
		R"({"parentUuid":null,"isSidechain":true,"promptId":"2cc28b71-f036-4e88-9337-1bef264ccce0","agentId":"ac9666ea1479fd4d4","type":"user","message":{"role":"user","content":"You are reviewing one completed task."},"uuid":"e1b112ee-9a59-43a4-9705-286a2503b963","timestamp":"2026-08-29T14:49:52.238Z","userType":"external","entrypoint":"cli","cwd":"D:\\Repos\\IDE","sessionId":"17eb9736-9ed9-4b84-9c52-6ed62e9fa11a","version":"2.1.251","gitBranch":"main"})"
		"\n"
		R"({"parentUuid":"51b5f0b5-7a76-43b6-88e0-561c0f03aa94","isSidechain":true,"agentId":"ac9666ea1479fd4d4","message":{"model":"claude-sonnet-5","id":"msg_011CeXE3fkEd3JEpP8VNw4Ja","type":"message","role":"assistant","content":[{"type":"tool_use","id":"toolu_01AVqVz1AcJ2gEPQe3sWtZMH","name":"Read","input":{"file_path":"D:\\Repos\\IDE\\plan.md"},"caller":{"type":"direct"}}],"stop_reason":null,"stop_sequence":null,"stop_details":null,"usage":{"input_tokens":2,"cache_creation_input_tokens":9091,"cache_read_input_tokens":27300,"cache_creation":{"ephemeral_5m_input_tokens":9091,"ephemeral_1h_input_tokens":0},"output_tokens":5,"service_tier":"standard","inference_geo":"not_available"},"diagnostics":null},"requestId":"req_011CeXE3ezqSkgFaGhts9bCq","attributionAgent":"general-purpose","type":"assistant","uuid":"66f5e979-3cfd-4508-80c9-268afbc78dc0","timestamp":"2026-08-29T14:49:56.219Z","effort":"medium","userType":"external","entrypoint":"cli","cwd":"D:\\Repos\\IDE","sessionId":"17eb9736-9ed9-4b84-9c52-6ed62e9fa11a","version":"2.1.251","gitBranch":"main"})"
		"\n"
		R"({"parentUuid":"66f5e979-3cfd-4508-80c9-268afbc78dc0","isSidechain":true,"promptId":"2cc28b71-f036-4e88-9337-1bef264ccce0","agentId":"ac9666ea1479fd4d4","type":"user","message":{"role":"user","content":[{"tool_use_id":"toolu_01AVqVz1AcJ2gEPQe3sWtZMH","type":"tool_result","content":"1\t### Task 1"}]},"uuid":"df3bd49c-021e-4355-8582-a3f877cf098f","timestamp":"2026-08-29T14:49:56.229Z","toolUseResult":{"type":"text","file":{"filePath":"D:\\Repos\\IDE\\plan.md","content":"1\t### Task 1","numLines":1,"startLine":1,"totalLines":1}},"sourceToolAssistantUUID":"66f5e979-3cfd-4508-80c9-268afbc78dc0","userType":"external","entrypoint":"cli","cwd":"D:\\Repos\\IDE","sessionId":"17eb9736-9ed9-4b84-9c52-6ed62e9fa11a","version":"2.1.251","gitBranch":"main"})"
		"\n"
		R"({"parentUuid":"1ce20c4f-7ad8-4f37-9209-8a54ef4b372d","isSidechain":true,"agentId":"ac9666ea1479fd4d4","message":{"model":"claude-sonnet-5","id":"msg_011CeXE53RjvQmBzVUE1uy6D","type":"message","role":"assistant","content":[{"type":"text","text":"## Verdict\n\nSpec compliance: yes."}],"stop_reason":null,"stop_sequence":null,"stop_details":null,"usage":{"input_tokens":2,"cache_creation_input_tokens":1439,"cache_read_input_tokens":47626,"cache_creation":{"ephemeral_5m_input_tokens":1439,"ephemeral_1h_input_tokens":0},"output_tokens":3,"service_tier":"standard","inference_geo":"not_available"},"diagnostics":null},"requestId":"req_011CeXE52dNQnFBEtmf1Gvc2","attributionAgent":"general-purpose","type":"assistant","uuid":"98ed36e5-5ca3-4a76-b387-2edbcd2cdcb6","timestamp":"2026-08-29T14:50:24.508Z","effort":"medium","userType":"external","entrypoint":"cli","cwd":"D:\\Repos\\IDE","sessionId":"17eb9736-9ed9-4b84-9c52-6ed62e9fa11a","version":"2.1.251","gitBranch":"main"})"
		"\n";

	Session session;
	session.Append(ParseTranscript(transcript).Records);

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
