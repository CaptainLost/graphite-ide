#include <doctest/doctest.h>

#include "Session/AgentInstall.h"

#include "TempDirectory.h"

#include <filesystem>

TEST_CASE("The agent's version is the name of the folder it was installed in")
{
	CHECK(Agent::VersionOf("/home/me/.graphite/agent/0123abcd4567ef89/graphite-agent") == "0123abcd4567ef89");
}

TEST_CASE("Starting removes the other versions installed beside this one")
{
	TempDirectory home;
	const std::filesystem::path self = home.MakeFile(".graphite/agent/new/graphite-agent", "binary");
	home.MakeFile(".graphite/agent/old/graphite-agent", "older binary");

	Agent::RemoveOtherVersions(self);

	CHECK(std::filesystem::exists(self));
	CHECK_FALSE(std::filesystem::exists(home.GetPath() / ".graphite" / "agent" / "old"));
}

TEST_CASE("An agent run from anywhere else removes nothing")
{
	TempDirectory somewhere;
	const std::filesystem::path self = somewhere.MakeFile("build/graphite-agent", "binary");
	somewhere.MakeFile("build-other/keep.txt", "keep");
	somewhere.MakeFile("unrelated.txt", "keep");

	Agent::RemoveOtherVersions(self);

	CHECK(std::filesystem::exists(somewhere.GetPath() / "build-other" / "keep.txt"));
	CHECK(std::filesystem::exists(somewhere.GetPath() / "unrelated.txt"));
}
