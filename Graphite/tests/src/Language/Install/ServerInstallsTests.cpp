#include <doctest/doctest.h>

#include "InstalledServers.h"
#include "Language/Install/InstalledServer.h"
#include "Language/Install/ServerInstalls.h"
#include "TempDirectory.h"

#include <filesystem>
#include <functional>
#include <optional>
#include <ostream>
#include <string>
#include <vector>

namespace {

	Core::JobSubmitter RunWhereSubmitted()
	{
		return [](std::function<void()> job) { job(); };
	}
}

TEST_CASE("With no HTTP client nothing installs and an unknown server is refused")
{
	TempDirectory temp;
	Graphite::ServerInstalls installs{nullptr, temp.GetPath() / "servers", RunWhereSubmitted()};

	CHECK_FALSE(installs.Install("csharp-ls"));
	CHECK_FALSE(installs.CheckForUpdates("csharp-ls"));
	CHECK_FALSE(installs.Uninstall("no-such-server"));
	CHECK_FALSE(installs.IsBusy("csharp-ls"));
}

TEST_CASE("An uninstall that ran is reported once as finished and its progress is kept")
{
	TempDirectory temp;
	const std::filesystem::path version = InstallCsharpLsIn(temp.GetPath() / "servers");
	Graphite::ServerInstalls installs{nullptr, temp.GetPath() / "servers", RunWhereSubmitted()};

	REQUIRE(installs.Uninstall("csharp-ls"));
	installs.Poll();

	std::vector<Graphite::InstallProgress> finished;
	installs.TakeFinished(finished);

	REQUIRE(finished.size() == 1);
	CHECK(finished[0].ServerId == "csharp-ls");
	CHECK(finished[0].Step == Graphite::InstallStep::Uninstalled);
	CHECK_FALSE(std::filesystem::exists(version));

	const std::optional<Graphite::InstallProgress> progress = installs.ProgressOf("csharp-ls");
	REQUIRE(progress.has_value());
	CHECK(progress->Step == Graphite::InstallStep::Uninstalled);

	std::vector<Graphite::InstallProgress> again;
	installs.TakeFinished(again);
	CHECK(again.empty());
}

TEST_CASE("The start-up sweep removes what an interrupted install left")
{
	TempDirectory temp;
	InstallCsharpLsIn(temp.GetPath() / "servers");
	const std::filesystem::path serverDirectory = Graphite::ServerDirectoryOf(temp.GetPath() / "servers", "csharp-ls");
	const std::filesystem::path leftover = temp.MakeFile("servers/csharp-ls/0.27.0/tools/old.dll", "");
	Graphite::ServerInstalls installs{nullptr, temp.GetPath() / "servers", RunWhereSubmitted()};

	installs.SweepAtStartup();

	CHECK_FALSE(std::filesystem::exists(leftover));
	CHECK(Graphite::FindInstalledServer(serverDirectory).has_value());
}

TEST_CASE("A server serves the languages that list it")
{
	CHECK(Graphite::LanguagesServedBy("roslyn") == std::vector<Graphite::LanguageId>{Graphite::LanguageId::CSharp});
	CHECK(Graphite::LanguagesServedBy("clangd") == std::vector<Graphite::LanguageId>{Graphite::LanguageId::Cpp});
	CHECK(Graphite::LanguagesServedBy("no-such-server").empty());
}
