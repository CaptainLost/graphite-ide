#include <doctest/doctest.h>

#include "Sessions/ClaudeLaunch.h"

#include "TempDirectory.h"

using namespace Graphite;

TEST_CASE("Transcripts are under the configured Claude directory, else under ~/.claude")
{
	CHECK(ClaudeProjectsDirectory(std::nullopt, "C:\\Users\\me") == std::filesystem::path{"C:\\Users\\me\\.claude\\projects"});
	CHECK(ClaudeProjectsDirectory(std::filesystem::path{"E:\\claude"}, "C:\\Users\\me") ==
		  std::filesystem::path{"E:\\claude\\projects"});
}

TEST_CASE("A configured program that exists wins over PATH")
{
	TempDirectory temp;
	const std::filesystem::path configured = temp.MakeFile("claude.exe", "");

	const auto onPath = [](const std::string&) { return std::filesystem::path{"C:\\bin\\claude.exe"}; };

	CHECK(ResolveClaudeProgram(configured, onPath) == configured);
	CHECK(ResolveClaudeProgram(temp.GetPath() / "missing.exe", onPath) == std::filesystem::path{"C:\\bin\\claude.exe"});
	CHECK(ResolveClaudeProgram({}, onPath) == std::filesystem::path{"C:\\bin\\claude.exe"});
}

TEST_CASE("No configured program and nothing on PATH is no program")
{
	const auto nowhere = [](const std::string&) { return std::filesystem::path{}; };

	CHECK(ResolveClaudeProgram({}, nowhere).empty());
}

TEST_CASE("A new session is started under the id the IDE chose, and a finished one resumed by its id")
{
	const ClaudeCommand start = NewSessionCommand("C:\\bin\\claude.exe", "1234");
	CHECK(start.Program == std::filesystem::path{"C:\\bin\\claude.exe"});
	CHECK(start.Arguments == std::vector<std::string>{"--session-id", "1234"});

	CHECK(ResumeCommand("C:\\bin\\claude.exe", "abcd").Arguments == std::vector<std::string>{"--resume", "abcd"});
}
