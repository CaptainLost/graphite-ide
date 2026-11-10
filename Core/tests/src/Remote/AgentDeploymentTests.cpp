#include <doctest/doctest.h>

#include "Core/Remote/AgentDeployment.h"

TEST_CASE("An agent's version is the first sixteen hex digits of its SHA-256")
{
	CHECK(Core::AgentVersionOf("abc") == "ba7816bf8f01cfea");
}

TEST_CASE("Two different binaries have different versions")
{
	CHECK(Core::AgentVersionOf("one build") != Core::AgentVersionOf("another build"));
}

TEST_CASE("A Windows path is named as the distro sees it, under /mnt")
{
	if (!Core::IsWslAvailable())
	{
		MESSAGE("WSL is not available here: skipped");
		return;
	}

	const Core::Result<std::string> linuxPath = Core::LinuxPathOf("", L"C:\\Windows");

	REQUIRE(linuxPath.HasValue());
	CHECK(linuxPath.GetValue() == "/mnt/c/Windows");
}

TEST_CASE("The agent is installed under ~/.graphite/agent/<version>/ and reached through the distro's share")
{
	const Core::AgentInstall install = Core::AgentInstallFor("Ubuntu-22.04", "/home/me", "0123456789abcdef");

	CHECK(install.LinuxPath == "/home/me/.graphite/agent/0123456789abcdef/graphite-agent");
	CHECK(install.SharePath == std::filesystem::path{L"\\\\wsl.localhost\\Ubuntu-22.04\\home\\me\\.graphite\\agent\\0123456789abcdef\\graphite-agent"});
	CHECK(install.Version == "0123456789abcdef");
}
