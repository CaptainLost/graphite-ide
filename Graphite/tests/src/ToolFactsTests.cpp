#include <doctest/doctest.h>

#include "Sessions/ToolFacts.h"

#include "TempDirectory.h"
#include "TranscriptFixtures.h"

using namespace Graphite;

namespace {
	ToolCall Call(const std::string& name, const nlohmann::json& result, const nlohmann::json& input = nlohmann::json::object(),
				  std::size_t bytes = 0)
	{
		ToolCall call;
		call.Name = name;
		call.InputJson = input.dump();
		call.ResultJson = result.is_null() ? std::string{} : result.dump();
		call.ResultBytes = bytes;
		call.HasResult = true;
		return call;
	}

	ToolFacts Facts(const ToolCall& call)
	{
		return FactsOf(call, {}, Pricing::Defaults());
	}
}

TEST_CASE("Tools fall into the categories the view colours by")
{
	CHECK(CategoryOf("Read") == ToolCategory::Read);
	CHECK(CategoryOf("Grep") == ToolCategory::Read);
	CHECK(CategoryOf("Edit") == ToolCategory::Edit);
	CHECK(CategoryOf("Write") == ToolCategory::Edit);
	CHECK(CategoryOf("Bash") == ToolCategory::Run);
	CHECK(CategoryOf("PowerShell") == ToolCategory::Run);
	CHECK(CategoryOf("Agent") == ToolCategory::Agent);
	CHECK(CategoryOf("Skill") == ToolCategory::Agent);
	CHECK(CategoryOf("WebFetch") == ToolCategory::Web);
	CHECK(CategoryOf("mcp__ghidra__list") == ToolCategory::Other);
}

TEST_CASE("Sizes read as bytes and tokens a person scans")
{
	CHECK(FormatBytes(812) == "812 B");
	CHECK(FormatBytes(3277) == "3.2 KB");
	CHECK(FormatBytes(1468006) == "1.4 MB");
	CHECK(FormatTokens(310) == "310");
	CHECK(FormatTokens(12400) == "12.4k");
	CHECK(FormatTokens(1200000) == "1.2M");
}

TEST_CASE("FormatTokensExact writes every digit, in groups of three")
{
	CHECK(FormatTokensExact(0) == "0");
	CHECK(FormatTokensExact(999) == "999");
	CHECK(FormatTokensExact(1000) == "1,000");
	CHECK(FormatTokensExact(51233707) == "51,233,707");
	CHECK(FormatTokensExact(100000) == "100,000");
}

TEST_CASE("A read reports the lines it read")
{
	CHECK(Facts(Call("Read", {{"type", "text"}, {"file", {{"numLines", 214}}}})).Size == "214 lines");
}

TEST_CASE("A command reports its output size, the stored size when the output was kept apart, and an interruption")
{
	CHECK(Facts(Call("Bash", {{"stdout", "abc"}, {"stderr", "de"}, {"interrupted", false}})).Size == "5 B");
	CHECK(Facts(Call("Bash", {{"stdout", "x"}, {"persistedOutputSize", 34342}, {"interrupted", false}})).Size == "33.5 KB");
	CHECK(Facts(Call("PowerShell", {{"stdout", ""}, {"interrupted", true}})).Detail == "interrupted");
}

TEST_CASE("An edit counts the lines its patch added and removed")
{
	const nlohmann::json patch = nlohmann::json::array(
		{{{"lines", {" keep", "-old one", "-old two", "+new"}}}, {{"lines", {"+another"}}}});

	CHECK(Facts(Call("Edit", {{"structuredPatch", patch}})).Detail == "+2 \xe2\x88\x92" "2");
}

TEST_CASE("A new file counts its lines")
{
	CHECK(Facts(Call("Write", {{"type", "create"}, {"structuredPatch", nlohmann::json::array()}, {"content", "a\nb\nc"}}))
			  .Detail == "new file, 3 lines");
}

TEST_CASE("A search reports its matches and files")
{
	CHECK(Facts(Call("Grep", {{"mode", "content"}, {"numLines", 3}, {"numFiles", 2}})).Detail == "3 matches in 2 files");
	CHECK(Facts(Call("Grep", {{"mode", "files_with_matches"}, {"numFiles", 4}})).Detail == "4 files");
}

TEST_CASE("A subagent reports its type, model and cost; with no transcript yet, no cost")
{
	const ToolCall call = Call("Agent", {{"agentId", "a1"}, {"resolvedModel", "claude-sonnet-5"}},
							   {{"subagent_type", "Explore"}, {"description", "Map"}});

	SubagentUsage subagents;
	subagents["a1"] = {{{"claude-sonnet-5", false}, {1'000'000, 0, 0, 0, 0}}};

	CHECK(FactsOf(call, subagents, Pricing::Defaults()).Detail == "Explore \xc2\xb7 sonnet-5 \xc2\xb7 $2.00");
	CHECK(FactsOf(call, {}, Pricing::Defaults()).Detail == "Explore \xc2\xb7 sonnet-5");
}

TEST_CASE("A missing or mistyped field costs the detail, never the row")
{
	CHECK(Facts(Call("Read", {{"file", "not an object"}})).Size.empty());
	CHECK(Facts(Call("Grep", nlohmann::json{})).Detail.empty());
	CHECK(Facts(Call("Edit", {{"structuredPatch", 7}})).Detail.empty());
}

TEST_CASE("A tool with no structured result falls back to the size of its text")
{
	CHECK(Facts(Call("mcp__x", nullptr, nlohmann::json::object(), 2048)).Size == "2.0 KB");
}

TEST_CASE("A usage line names the model, the cached share, output and cost")
{
	const UsageByModel usage{{{"claude-opus-5", false}, {2'000, 310, 0, 0, 198'000}}};

	CHECK(UsageLine(usage, 1, Pricing::Defaults()) ==
		  "1 request \xc2\xb7 opus-5 \xc2\xb7 200.0k in (99% cache) \xc2\xb7 310 out \xc2\xb7 $0.12");
	CHECK(UsageLine(usage, 3, Pricing::Defaults()).starts_with("3 requests \xc2\xb7 opus-5"));
}

TEST_CASE("Subagent usage is read per agent id from the session's subagents folder")
{
	TempDirectory temp;
	const std::filesystem::path file = temp.MakeFile("p/s.jsonl", Fixtures::UserPrompt("x"));
	temp.MakeFile("p/s/subagents/agent-a1.jsonl", Fixtures::Assistant("r", Fixtures::TextBlock("y"), Fixtures::Usage(5, 6)));

	const SubagentUsage usage = LoadSubagentUsage(file);

	REQUIRE(usage.contains("a1"));
	CHECK(usage.at("a1") == UsageByModel{{{"claude-opus-5", false}, {5, 6, 0, 0, 0}}});
}

TEST_CASE("A cached share is a whole percent, cut rather than rounded")
{
	// 99.9% is not all of it: rounded, it would read as 100%.
	CHECK(CachedPercent(999, 1000) == 99);
	CHECK(CachedPercent(1000, 1000) == 100);
	CHECK(CachedPercent(0, 1000) == 0);
	CHECK(CachedPercent(5, 0) == 0);

	const UsageByModel usage{{{"claude-opus-5", false}, {1, 1, 0, 0, 999}}};
	CHECK(UsageLine(usage, 1, Pricing::Defaults()).find("(99% cache)") != std::string::npos);
}

TEST_CASE("Only Agent and Task calls start a subagent")
{
	CHECK(IsAgentCall(Call("Agent", nullptr)));
	CHECK(IsAgentCall(Call("Task", nullptr)));
	CHECK_FALSE(IsAgentCall(Call("Read", nullptr)));
	CHECK_FALSE(IsAgentCall(Call("SendMessage", nullptr)));
	CHECK_FALSE(IsAgentCall(Call("Workflow", nullptr)));
}
