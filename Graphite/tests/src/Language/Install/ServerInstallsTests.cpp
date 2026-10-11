#include <doctest/doctest.h>

#include "DeferredJobs.h"
#include "FakeHttpClient.h"
#include "InstalledServers.h"
#include "Language/Install/InstalledServer.h"
#include "Language/Install/ServerInstalls.h"
#include "TempDirectory.h"
#include "ZipFixtures.h"

#include <nlohmann/json.hpp>

#include <filesystem>
#include <format>
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

	void ServeClangd(FakeHttpClient& http, const std::string& version)
	{
		const std::string assetName = std::format("clangd-windows-{}.zip", version);
		const std::string downloadUrl = std::format("https://github.com/clangd/clangd/releases/download/{}/{}", version, assetName);

		nlohmann::json asset = nlohmann::json::object();
		asset["name"] = assetName;
		asset["browser_download_url"] = downloadUrl;

		nlohmann::json release = nlohmann::json::object();
		release["tag_name"] = version;
		release["assets"] = nlohmann::json::array();
		release["assets"].push_back(asset);

		http.Answer("https://api.github.com/repos/clangd/clangd/releases/latest", 200, release.dump());
		http.Answer(downloadUrl, 200, ZipBytes({{.Name = std::format("clangd_{}/bin/clangd.exe", version), .Contents = "clangd"}}));
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

TEST_CASE("What is installed is read after the sweep and known once polled")
{
	TempDirectory temp;
	InstallCsharpLsIn(temp.GetPath() / "servers");
	Graphite::ServerInstalls installs{nullptr, temp.GetPath() / "servers", RunWhereSubmitted()};

	CHECK_FALSE(installs.HasReadInstalls());

	installs.SweepAtStartup();
	CHECK_FALSE(installs.HasReadInstalls());

	installs.Poll();

	CHECK(installs.HasReadInstalls());
	CHECK(installs.InstalledVersionOf("csharp-ls") == Graphite::InstalledVersion{.Version = "0.28.0", .HasChecksum = false});
	CHECK_FALSE(installs.InstalledVersionOf("roslyn").has_value());
}

TEST_CASE("An install records the version it placed and its job ends")
{
	TempDirectory temp;
	const Core::Ref<FakeHttpClient> http = Core::CreateRef<FakeHttpClient>();
	ServeClangd(*http, "23.1.0");
	Graphite::ServerInstalls installs{http, temp.GetPath() / "servers", RunWhereSubmitted()};

	REQUIRE(installs.Install("clangd"));
	installs.Poll();

	CHECK(installs.InstalledVersionOf("clangd") == Graphite::InstalledVersion{.Version = "23.1.0", .HasChecksum = false});
	CHECK_FALSE(installs.RunningJobOf("clangd").has_value());
}

TEST_CASE("An install that finds the version already there records it")
{
	TempDirectory temp;
	const Core::Ref<FakeHttpClient> firstHttp = Core::CreateRef<FakeHttpClient>();
	const Core::Ref<FakeHttpClient> secondHttp = Core::CreateRef<FakeHttpClient>();
	ServeClangd(*firstHttp, "23.1.0");
	ServeClangd(*secondHttp, "23.1.0");
	Graphite::ServerInstalls first{firstHttp, temp.GetPath() / "servers", RunWhereSubmitted()};
	Graphite::ServerInstalls second{secondHttp, temp.GetPath() / "servers", RunWhereSubmitted()};

	second.SweepAtStartup();
	second.Poll();
	CHECK_FALSE(second.InstalledVersionOf("clangd").has_value());

	REQUIRE(first.Install("clangd"));
	first.Poll();

	REQUIRE(second.Install("clangd"));
	second.Poll();

	REQUIRE(second.ProgressOf("clangd").has_value());
	CHECK(second.ProgressOf("clangd")->Step == Graphite::InstallStep::UpToDate);
	REQUIRE(second.InstalledVersionOf("clangd").has_value());
	CHECK(second.InstalledVersionOf("clangd")->Version == "23.1.0");
}

TEST_CASE("A job is known by its kind from the moment it is asked for until its outcome is polled")
{
	TempDirectory temp;
	InstallCsharpLsIn(temp.GetPath() / "servers");
	DeferredJobs jobs;
	Graphite::ServerInstalls installs{nullptr, temp.GetPath() / "servers", jobs.Submitter()};

	installs.SweepAtStartup();
	jobs.RunAll();
	installs.Poll();

	REQUIRE(installs.Uninstall("csharp-ls"));
	CHECK(installs.RunningJobOf("csharp-ls") == Graphite::InstallJobKind::Uninstall);

	jobs.RunAll();
	CHECK(installs.RunningJobOf("csharp-ls") == Graphite::InstallJobKind::Uninstall);

	installs.Poll();

	CHECK_FALSE(installs.RunningJobOf("csharp-ls").has_value());
	CHECK_FALSE(installs.InstalledVersionOf("csharp-ls").has_value());
}

TEST_CASE("A cancel asked for is known until the job's outcome is polled")
{
	TempDirectory temp;
	DeferredJobs jobs;
	Graphite::ServerInstalls installs{Core::CreateRef<FakeHttpClient>(), temp.GetPath() / "servers", jobs.Submitter()};

	REQUIRE(installs.Install("clangd"));
	CHECK_FALSE(installs.IsCancelling("clangd"));

	installs.Cancel("clangd");
	CHECK(installs.IsCancelling("clangd"));

	jobs.RunAll();
	installs.Poll();

	CHECK_FALSE(installs.IsCancelling("clangd"));
	REQUIRE(installs.ProgressOf("clangd").has_value());
	CHECK(installs.ProgressOf("clangd")->Step == Graphite::InstallStep::Cancelled);
}

TEST_CASE("Downloads are possible only with an HTTP client")
{
	TempDirectory temp;

	CHECK_FALSE(Graphite::ServerInstalls{nullptr, temp.GetPath(), RunWhereSubmitted()}.CanDownload());
	CHECK(Graphite::ServerInstalls{Core::CreateRef<FakeHttpClient>(), temp.GetPath(), RunWhereSubmitted()}.CanDownload());
}

TEST_CASE("A job for a server waits until the last one's outcome has been polled")
{
	TempDirectory temp;
	InstallCsharpLsIn(temp.GetPath() / "servers");
	DeferredJobs jobs;
	Graphite::ServerInstalls installs{nullptr, temp.GetPath() / "servers", jobs.Submitter()};

	installs.SweepAtStartup();
	jobs.RunAll();
	installs.Poll();

	REQUIRE(installs.Uninstall("csharp-ls"));
	jobs.RunAll();

	CHECK_FALSE(installs.Uninstall("csharp-ls"));
	CHECK(installs.IsBusy("csharp-ls"));
	CHECK(installs.RunningJobOf("csharp-ls") == Graphite::InstallJobKind::Uninstall);

	installs.Poll();

	CHECK_FALSE(installs.IsBusy("csharp-ls"));
	CHECK_FALSE(installs.RunningJobOf("csharp-ls").has_value());
}
