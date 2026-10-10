#include <doctest/doctest.h>

#include "Language/Install/InstalledServer.h"
#include "Language/Install/ServerInstaller.h"

#include "Core/Base/ByteText.h"
#include "Core/Base/ScopeExit.h"
#include "Core/FileSystem/FileLock.h"
#include "Core/FileSystem/FileSystem.h"
#include "Core/Jobs/JobQueue.h"
#include "Core/Platform/Hash.h"

#include "FakeHttpClient.h"
#include "TempDirectory.h"
#include "ZipFixtures.h"

#include <nlohmann/json.hpp>

#include <array>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <format>
#include <fstream>
#include <functional>
#include <optional>
#include <ostream>
#include <string>
#include <thread>
#include <vector>

#ifdef CORE_PLATFORM_WINDOWS
#include <windows.h>
#endif

namespace {

	constexpr Core::PlatformTarget kWindowsX64{.System = Core::OperatingSystem::Windows, .Machine = Core::Architecture::X64};

	constexpr std::array<Graphite::GitHubAsset, 1> kToolAssets{{
		{.Platform = kWindowsX64,
		 .AssetTemplate = "tool-{os}-{version}.zip",
		 .Archive = Graphite::ArchiveKind::Zip,
		 .ExecutableTemplate = "tool_{version}/bin/tool{exe}"},
	}};

	constexpr Graphite::ServerDefinition kGitHubTool{
		.Id = "tool",
		.DisplayName = "Tool",
		.ExecutableName = "tool",
		.ProtocolLanguageId = "cpp",
		.Package = Graphite::GitHubReleasePackage{.Repository = "example/tool", .Assets = kToolAssets}};

	constexpr Graphite::ServerDefinition kNuGetTool{
		.Id = "example-tool",
		.DisplayName = "Example Tool",
		.ExecutableName = "example-tool",
		.ProtocolLanguageId = "csharp",
		.Package = Graphite::NuGetToolPackage{.PackageIdTemplate = "Example.Tool", .AllowPrerelease = false}};

	constexpr const char* kLatestUrl = "https://api.github.com/repos/example/tool/releases/latest";

	Core::JobSubmitter RunWhereSubmitted()
	{
		return [](std::function<void()> job) { job(); };
	}

	std::string DownloadUrlOf(const std::string& version)
	{
		return std::format("https://github.com/example/tool/releases/download/{0}/tool-windows-{0}.zip", version);
	}

	std::string Sha256Of(const std::string& bytes)
	{
		const Core::Result<std::vector<std::uint8_t>> digest = Core::HashBytes(Core::HashAlgorithm::Sha256, bytes);
		REQUIRE(digest);
		return "sha256:" + Core::BytesToHex(digest.GetValue());
	}

	// xorshift32: bytes a zip cannot shrink, so an archive spans several of the fake's download pieces.
	std::string UncompressibleBytes(std::size_t count)
	{
		std::string bytes(count, '\0');
		std::uint32_t state = 2463534242u;

		for (char& byte : bytes)
		{
			state ^= state << 13;
			state ^= state >> 17;
			state ^= state << 5;
			byte = static_cast<char>(state & 0xFF);
		}

		return bytes;
	}

	std::string ToolZip(const std::string& version, const std::string& serverBytes = "the server")
	{
		return ZipBytes({{.Name = std::format("tool_{}/bin/tool.exe", version), .Contents = serverBytes},
						 {.Name = std::format("tool_{}/lib/runtime.dll", version), .Contents = "a library"}});
	}

	void ServeRelease(FakeHttpClient& http, const std::string& version, const std::string& archive,
					  const std::optional<std::string>& digest)
	{
		nlohmann::json asset = nlohmann::json::object();
		asset["name"] = std::format("tool-windows-{}.zip", version);
		asset["browser_download_url"] = DownloadUrlOf(version);

		if (digest)
		{
			asset["digest"] = *digest;
		}

		nlohmann::json release = nlohmann::json::object();
		release["tag_name"] = version;
		release["assets"] = nlohmann::json::array();
		release["assets"].push_back(asset);

		http.Answer(kLatestUrl, 200, release.dump());
		http.Answer(DownloadUrlOf(version), 200, archive);
	}

	void ServeNuGetTool(FakeHttpClient& http, const std::string& version, const std::string& package)
	{
		const std::string catalogUrl =
			std::format("https://api.nuget.org/v3/catalog0/data/2026.10.01.00.00.00/example.tool.{}.json", version);

		nlohmann::json catalogEntry = nlohmann::json::object();
		catalogEntry["@id"] = catalogUrl;
		catalogEntry["version"] = version;
		catalogEntry["listed"] = true;

		nlohmann::json leaf = nlohmann::json::object();
		leaf["catalogEntry"] = catalogEntry;

		nlohmann::json page = nlohmann::json::object();
		page["@id"] = "https://api.nuget.org/v3/registration5-gz-semver2/example.tool/index.json#page/0";
		page["items"] = nlohmann::json::array();
		page["items"].push_back(leaf);

		nlohmann::json index = nlohmann::json::object();
		index["count"] = 1;
		index["items"] = nlohmann::json::array();
		index["items"].push_back(page);

		http.Answer("https://api.nuget.org/v3/registration5-gz-semver2/example.tool/index.json", 200, index.dump());
		http.Answer(catalogUrl, 200, nlohmann::json::object().dump());
		http.Answer(std::format("https://api.nuget.org/v3-flatcontainer/example.tool/{0}/example.tool.{0}.nupkg", version),
					200, package);
	}

	std::vector<Graphite::InstallStep> StepsOf(const std::vector<Graphite::InstallProgress>& changes)
	{
		std::vector<Graphite::InstallStep> steps;

		for (const Graphite::InstallProgress& change : changes)
		{
			steps.push_back(change.Step);
		}

		return steps;
	}

	std::size_t CountOfStep(const std::vector<Graphite::InstallProgress>& changes, Graphite::InstallStep step)
	{
		std::size_t count = 0;

		for (const Graphite::InstallProgress& change : changes)
		{
			if (change.Step == step)
			{
				++count;
			}
		}

		return count;
	}

	std::size_t CountOfRequestsFor(const FakeHttpClient& http, const std::string& url)
	{
		std::size_t count = 0;

		for (const Core::HttpRequest& request : http.Requests)
		{
			if (request.Url == url)
			{
				++count;
			}
		}

		return count;
	}

	struct InstallerFixture
	{
		TempDirectory Temporary;
		std::filesystem::path Servers = Temporary.GetPath() / Core::FromUtf8("Zażółć Ω") / "servers";
		Core::Ref<FakeHttpClient> Http = Core::CreateRef<FakeHttpClient>();
		Graphite::ServerInstaller Installer{Http, Servers, kWindowsX64, RunWhereSubmitted()};

		std::vector<Graphite::InstallProgress> Drain()
		{
			std::vector<Graphite::InstallProgress> changes;
			Installer.Drain(changes);
			return changes;
		}

		std::filesystem::path ToolDirectory() const { return Servers / "tool"; }

		void InstallVersion(const std::string& version)
		{
			const std::string archive = ToolZip(version);
			ServeRelease(*Http, version, archive, Sha256Of(archive));
			REQUIRE(Installer.Install(kGitHubTool));
			const std::vector<Graphite::InstallProgress> changes = Drain();
			REQUIRE_FALSE(changes.empty());
			REQUIRE(changes.back().Step == Graphite::InstallStep::Installed);
		}
	};
}

TEST_CASE("Installing a GitHub release downloads and verifies and extracts and records it")
{
	InstallerFixture fixture;
	const std::string archive = ToolZip("1.0.0");
	ServeRelease(*fixture.Http, "1.0.0", archive, Sha256Of(archive));

	REQUIRE(fixture.Installer.Install(kGitHubTool));
	const std::vector<Graphite::InstallProgress> changes = fixture.Drain();

	using Graphite::InstallStep;
	CHECK(StepsOf(changes) == std::vector<InstallStep>{InstallStep::Resolving, InstallStep::Downloading,
													   InstallStep::Verifying, InstallStep::Extracting, InstallStep::Installed});
	REQUIRE(changes.size() == 5);
	CHECK(changes[1].ReceivedBytes == archive.size());
	CHECK(changes[1].TotalBytes == archive.size());
	CHECK(changes.back().ServerId == "tool");
	CHECK(changes.back().Version == "1.0.0");
	CHECK(changes.back().HasChecksum);
	CHECK_FALSE(fixture.Installer.IsBusy("tool"));

	const std::optional<Graphite::InstalledServer> installed = Graphite::FindInstalledServer(fixture.ToolDirectory());

	REQUIRE(installed.has_value());
	CHECK(installed->Command == fixture.ToolDirectory() / "1.0.0" / "tool_1.0.0" / "bin" / "tool.exe");
	CHECK(installed->Record.SourceUrl == DownloadUrlOf("1.0.0"));
	CHECK(installed->Record.Command.Runner == Graphite::LaunchRunner::Executable);
	CHECK(installed->Record.VerifiedChecksum.has_value());
	CHECK_FALSE(installed->Record.InstalledAt.empty());
	CHECK(std::filesystem::exists(fixture.ToolDirectory() / "1.0.0" / "tool_1.0.0" / "lib" / "runtime.dll"));
	CHECK_FALSE(std::filesystem::exists(fixture.ToolDirectory() / ".download"));
	CHECK_FALSE(std::filesystem::exists(fixture.ToolDirectory() / "1.0.0.tmp"));
	CHECK_FALSE(std::filesystem::exists(fixture.ToolDirectory() / "install.lock"));
}

TEST_CASE("Download progress for one server collapses into its latest between two drains")
{
	InstallerFixture fixture;
	const std::string archive = ToolZip("1.0.0", UncompressibleBytes(5000));
	ServeRelease(*fixture.Http, "1.0.0", archive, std::nullopt);
	REQUIRE(archive.size() > 2 * FakeHttpClient::kPieceBytes);

	REQUIRE(fixture.Installer.Install(kGitHubTool));
	const std::vector<Graphite::InstallProgress> changes = fixture.Drain();

	CHECK(CountOfStep(changes, Graphite::InstallStep::Downloading) == 1);
	REQUIRE(changes.size() > 1);
	CHECK(changes[1].Step == Graphite::InstallStep::Downloading);
	CHECK(changes[1].ReceivedBytes == archive.size());
}

TEST_CASE("A release without a checksum installs and says so")
{
	InstallerFixture fixture;
	ServeRelease(*fixture.Http, "1.0.0", ToolZip("1.0.0"), std::nullopt);

	REQUIRE(fixture.Installer.Install(kGitHubTool));
	const std::vector<Graphite::InstallProgress> changes = fixture.Drain();

	REQUIRE_FALSE(changes.empty());
	CHECK(changes.back().Step == Graphite::InstallStep::Installed);
	CHECK_FALSE(changes.back().HasChecksum);
	CHECK(CountOfStep(changes, Graphite::InstallStep::Verifying) == 0);

	const std::optional<Graphite::InstalledServer> installed = Graphite::FindInstalledServer(fixture.ToolDirectory());
	REQUIRE(installed.has_value());
	CHECK_FALSE(installed->Record.VerifiedChecksum.has_value());
}

TEST_CASE("A checksum mismatch deletes the download and installs nothing")
{
	InstallerFixture fixture;
	ServeRelease(*fixture.Http, "1.0.0", ToolZip("1.0.0"), Sha256Of("other bytes"));

	REQUIRE(fixture.Installer.Install(kGitHubTool));
	const std::vector<Graphite::InstallProgress> changes = fixture.Drain();

	REQUIRE_FALSE(changes.empty());
	CHECK(changes.back().Step == Graphite::InstallStep::Failed);
	CHECK(changes.back().Failure.find("checksum does not match") != std::string::npos);
	CHECK_FALSE(Graphite::FindInstalledServer(fixture.ToolDirectory()).has_value());
	CHECK_FALSE(std::filesystem::exists(fixture.ToolDirectory() / "1.0.0"));
	CHECK_FALSE(std::filesystem::exists(fixture.ToolDirectory() / ".download"));
}

TEST_CASE("Cancelling an update mid-download keeps the installed version and leaves nothing else")
{
	InstallerFixture fixture;
	fixture.InstallVersion("1.0.0");

	const std::string secondArchive = ToolZip("2.0.0", UncompressibleBytes(5000));
	ServeRelease(*fixture.Http, "2.0.0", secondArchive, Sha256Of(secondArchive));
	fixture.Http->AfterPiece = [&fixture](std::uint64_t) { fixture.Installer.Cancel("tool"); };

	REQUIRE(fixture.Installer.Install(kGitHubTool));
	const std::vector<Graphite::InstallProgress> changes = fixture.Drain();

	REQUIRE_FALSE(changes.empty());
	CHECK(changes.back().Step == Graphite::InstallStep::Cancelled);
	CHECK(changes.back().Failure.empty());

	const std::optional<Graphite::InstalledServer> installed = Graphite::FindInstalledServer(fixture.ToolDirectory());
	REQUIRE(installed.has_value());
	CHECK(installed->Record.Version == "1.0.0");
	CHECK_FALSE(std::filesystem::exists(fixture.ToolDirectory() / "2.0.0"));
	CHECK_FALSE(std::filesystem::exists(fixture.ToolDirectory() / "2.0.0.tmp"));
	CHECK_FALSE(std::filesystem::exists(fixture.ToolDirectory() / ".download"));
}

TEST_CASE("A NuGet tool installs with the command its settings name")
{
	InstallerFixture fixture;
	const std::string package = ZipBytes({
		{.Name = "Example.Tool.nuspec", .Contents = "<package />"},
		{.Name = "tools/net10.0/any/DotnetToolSettings.xml",
		 .Contents = "<DotNetCliTool Version=\"1\"><Commands><Command Name=\"example-tool\" EntryPoint=\"Example.Tool.dll\" "
					 "Runner=\"dotnet\" /></Commands></DotNetCliTool>"},
		{.Name = "tools/net10.0/any/Example.Tool.dll", .Contents = "an assembly"},
	});
	ServeNuGetTool(*fixture.Http, "1.2.0", package);

	REQUIRE(fixture.Installer.Install(kNuGetTool));
	const std::vector<Graphite::InstallProgress> changes = fixture.Drain();

	REQUIRE_FALSE(changes.empty());
	CHECK(changes.back().Step == Graphite::InstallStep::Installed);
	CHECK(changes.back().Version == "1.2.0");

	const std::filesystem::path serverDirectory = fixture.Servers / "example-tool";
	const std::optional<Graphite::InstalledServer> installed = Graphite::FindInstalledServer(serverDirectory);

	REQUIRE(installed.has_value());
	CHECK(installed->Record.Command == Graphite::InstalledCommand{.Runner = Graphite::LaunchRunner::Dotnet,
																  .RelativePath = "tools/net10.0/any/Example.Tool.dll"});
	CHECK(installed->Command == serverDirectory / "1.2.0" / "tools" / "net10.0" / "any" / "Example.Tool.dll");
}

TEST_CASE("An archive without the server inside fails and leaves nothing")
{
	InstallerFixture fixture;
	ServeRelease(*fixture.Http, "1.0.0", ZipBytes({{.Name = "README.md", .Contents = "no server here"}}), std::nullopt);

	REQUIRE(fixture.Installer.Install(kGitHubTool));
	const std::vector<Graphite::InstallProgress> changes = fixture.Drain();

	REQUIRE_FALSE(changes.empty());
	CHECK(changes.back().Step == Graphite::InstallStep::Failed);
	CHECK(changes.back().Failure.find("tool_1.0.0/bin/tool.exe") != std::string::npos);
	CHECK_FALSE(std::filesystem::exists(fixture.ToolDirectory() / "1.0.0"));
	CHECK_FALSE(std::filesystem::exists(fixture.ToolDirectory() / "1.0.0.tmp"));
	CHECK_FALSE(std::filesystem::exists(Graphite::RecordFileOf(fixture.ToolDirectory())));
}

TEST_CASE("A second job for a server is refused while the first one waits or runs")
{
	TempDirectory temporaryDirectory;
	std::vector<std::function<void()>> waitingJobs;
	const Core::Ref<FakeHttpClient> http = Core::CreateRef<FakeHttpClient>();
	Graphite::ServerInstaller installer{http, temporaryDirectory.GetPath() / "servers", kWindowsX64,
										[&waitingJobs](std::function<void()> job) { waitingJobs.push_back(std::move(job)); }};

	CHECK(installer.Install(kGitHubTool));
	CHECK(installer.IsBusy("tool"));
	CHECK_FALSE(installer.Install(kGitHubTool));
	CHECK_FALSE(installer.Uninstall(kGitHubTool));
	CHECK(installer.CheckForUpdates(kNuGetTool));

	REQUIRE(waitingJobs.size() == 2);

	for (const std::function<void()>& job : waitingJobs)
	{
		job();
	}

	CHECK_FALSE(installer.IsBusy("tool"));
	CHECK_FALSE(installer.IsBusy("example-tool"));
}

TEST_CASE("An install another Graphite window holds the lock for is refused with a reason")
{
	InstallerFixture fixture;
	std::filesystem::create_directories(fixture.ToolDirectory());
	const Core::Result<Core::FileLock> otherWindow = Core::FileLock::TryAcquire(Graphite::LockFileOf(fixture.ToolDirectory()));
	REQUIRE(otherWindow);
	ServeRelease(*fixture.Http, "1.0.0", ToolZip("1.0.0"), std::nullopt);

	REQUIRE(fixture.Installer.Install(kGitHubTool));
	const std::vector<Graphite::InstallProgress> changes = fixture.Drain();

	REQUIRE_FALSE(changes.empty());
	CHECK(changes.back().Step == Graphite::InstallStep::Failed);
	CHECK(changes.back().Failure == "another Graphite window is installing Tool");
	CHECK(fixture.Http->Requests.empty());
}

TEST_CASE("Checking for updates compares the latest release with the installed one")
{
	InstallerFixture fixture;
	fixture.InstallVersion("1.0.0");

	ServeRelease(*fixture.Http, "1.1.0", ToolZip("1.1.0"), std::nullopt);
	REQUIRE(fixture.Installer.CheckForUpdates(kGitHubTool));
	const std::vector<Graphite::InstallProgress> newer = fixture.Drain();

	REQUIRE_FALSE(newer.empty());
	CHECK(newer.back().Step == Graphite::InstallStep::UpdateAvailable);
	CHECK(newer.back().Version == "1.1.0");
	CHECK(CountOfRequestsFor(*fixture.Http, DownloadUrlOf("1.1.0")) == 0);

	ServeRelease(*fixture.Http, "1.0.0", ToolZip("1.0.0"), std::nullopt);
	REQUIRE(fixture.Installer.CheckForUpdates(kGitHubTool));
	const std::vector<Graphite::InstallProgress> same = fixture.Drain();

	REQUIRE_FALSE(same.empty());
	CHECK(same.back().Step == Graphite::InstallStep::UpToDate);
	CHECK(same.back().Version == "1.0.0");
}

TEST_CASE("A NuGet release is newer by version order and a GitHub release by any other tag")
{
	const Graphite::ServerPackage nuget = Graphite::NuGetToolPackage{.PackageIdTemplate = "x", .AllowPrerelease = true};
	const Graphite::ServerPackage github = Graphite::GitHubReleasePackage{.Repository = "example/tool", .Assets = kToolAssets};

	CHECK(Graphite::IsNewerRelease(nuget, "5.12.0-1.26475.2", "5.12.0-1.26426.8"));
	CHECK_FALSE(Graphite::IsNewerRelease(nuget, "5.12.0-1.26426.8", "5.12.0-1.26475.2"));
	CHECK_FALSE(Graphite::IsNewerRelease(nuget, "1.0.0", "1.0.0"));
	CHECK(Graphite::IsNewerRelease(github, "23.1.0", "22.1.0"));
	CHECK_FALSE(Graphite::IsNewerRelease(github, "23.1.0", "23.1.0"));
	CHECK_FALSE(Graphite::IsNewerRelease(github, "1.0.0-RC", "1.0.0-rc"));
}

TEST_CASE("Uninstalling removes the record and the version directory")
{
	InstallerFixture fixture;
	fixture.InstallVersion("1.0.0");

	REQUIRE(fixture.Installer.Uninstall(kGitHubTool));
	const std::vector<Graphite::InstallProgress> changes = fixture.Drain();

	REQUIRE_FALSE(changes.empty());
	CHECK(changes.back().Step == Graphite::InstallStep::Uninstalled);
	CHECK(changes.back().Version == "1.0.0");
	CHECK_FALSE(std::filesystem::exists(Graphite::RecordFileOf(fixture.ToolDirectory())));
	CHECK_FALSE(std::filesystem::exists(fixture.ToolDirectory() / "1.0.0"));
	CHECK_FALSE(Graphite::FindInstalledServer(fixture.ToolDirectory()).has_value());
}

TEST_CASE("Installing the version already installed downloads nothing")
{
	InstallerFixture fixture;
	fixture.InstallVersion("1.0.0");
	fixture.Http->Requests.clear();

	REQUIRE(fixture.Installer.Install(kGitHubTool));
	const std::vector<Graphite::InstallProgress> changes = fixture.Drain();

	REQUIRE_FALSE(changes.empty());
	CHECK(changes.back().Step == Graphite::InstallStep::UpToDate);
	CHECK(changes.back().Version == "1.0.0");
	CHECK(changes.back().HasChecksum);
	REQUIRE(fixture.Http->Requests.size() == 1);
	CHECK(fixture.Http->Requests.front().Url == kLatestUrl);
}

TEST_CASE("A release whose tag differs from the installed version only in case is the installed version")
{
	InstallerFixture fixture;
	fixture.InstallVersion("1.0.0-RC");
	ServeRelease(*fixture.Http, "1.0.0-rc", ToolZip("1.0.0-rc"), std::nullopt);
	fixture.Http->Requests.clear();

	REQUIRE(fixture.Installer.Install(kGitHubTool));
	const std::vector<Graphite::InstallProgress> installAgain = fixture.Drain();

	REQUIRE_FALSE(installAgain.empty());
	CHECK(installAgain.back().Step == Graphite::InstallStep::UpToDate);
	CHECK(CountOfRequestsFor(*fixture.Http, DownloadUrlOf("1.0.0-rc")) == 0);

	REQUIRE(fixture.Installer.CheckForUpdates(kGitHubTool));
	const std::vector<Graphite::InstallProgress> check = fixture.Drain();

	REQUIRE_FALSE(check.empty());
	CHECK(check.back().Step == Graphite::InstallStep::UpToDate);

	const std::optional<Graphite::InstalledServer> installed = Graphite::FindInstalledServer(fixture.ToolDirectory());
	REQUIRE(installed.has_value());
	CHECK(installed->Record.Version == "1.0.0-RC");
}

TEST_CASE("Checking for updates calls a server whose command is gone not up to date")
{
	InstallerFixture fixture;
	fixture.InstallVersion("1.0.0");
	REQUIRE(std::filesystem::remove(fixture.ToolDirectory() / "1.0.0" / "tool_1.0.0" / "bin" / "tool.exe"));

	REQUIRE(fixture.Installer.CheckForUpdates(kGitHubTool));
	const std::vector<Graphite::InstallProgress> changes = fixture.Drain();

	REQUIRE_FALSE(changes.empty());
	CHECK(changes.back().Step == Graphite::InstallStep::UpdateAvailable);
	CHECK(changes.back().Version == "1.0.0");
}

TEST_CASE("A job cancelled before it runs ends as cancelled without asking the network or making its folder")
{
	TempDirectory temporaryDirectory;
	const std::filesystem::path servers = temporaryDirectory.GetPath() / "servers";
	std::vector<std::function<void()>> waitingJobs;
	const Core::Ref<FakeHttpClient> http = Core::CreateRef<FakeHttpClient>();
	ServeRelease(*http, "1.0.0", ToolZip("1.0.0"), std::nullopt);
	Graphite::ServerInstaller installer{http, servers, kWindowsX64,
										[&waitingJobs](std::function<void()> job) { waitingJobs.push_back(std::move(job)); }};

	REQUIRE(installer.Install(kGitHubTool));
	installer.Cancel("tool");
	REQUIRE(waitingJobs.size() == 1);
	waitingJobs.front()();

	std::vector<Graphite::InstallProgress> changes;
	installer.Drain(changes);

	REQUIRE(changes.size() == 1);
	CHECK(changes.back().Step == Graphite::InstallStep::Cancelled);
	CHECK(changes.back().Failure.empty());
	CHECK(http->Requests.empty());
	CHECK_FALSE(std::filesystem::exists(Graphite::ServerDirectoryOf(servers, "tool")));
	CHECK_FALSE(installer.IsBusy("tool"));
}

TEST_CASE("A cancel that lands during the version lookup ends as cancelled even when the lookup fails")
{
	InstallerFixture fixture;
	fixture.Http->BeforeAnswer = [&fixture](const Core::HttpRequest&) { fixture.Installer.Cancel("tool"); };

	SUBCASE("installing")
	{
		REQUIRE(fixture.Installer.Install(kGitHubTool));
	}

	SUBCASE("checking for updates")
	{
		REQUIRE(fixture.Installer.CheckForUpdates(kGitHubTool));
	}

	const std::vector<Graphite::InstallProgress> changes = fixture.Drain();

	REQUIRE_FALSE(changes.empty());
	CHECK(changes.back().Step == Graphite::InstallStep::Cancelled);
	CHECK(changes.back().Failure.empty());
	REQUIRE(fixture.Http->Requests.size() == 1);
	CHECK(fixture.Http->Requests.front().Url == kLatestUrl);
}

TEST_CASE("A download that fails after a cancel ends as cancelled")
{
	InstallerFixture fixture;
	ServeRelease(*fixture.Http, "1.0.0", ToolZip("1.0.0"), std::nullopt);
	fixture.Http->Answer(DownloadUrlOf("1.0.0"), 503, "busy");
	fixture.Http->BeforeAnswer = [&fixture](const Core::HttpRequest& request) {
		if (request.Url == DownloadUrlOf("1.0.0"))
		{
			fixture.Installer.Cancel("tool");
		}
	};

	REQUIRE(fixture.Installer.Install(kGitHubTool));
	const std::vector<Graphite::InstallProgress> changes = fixture.Drain();

	REQUIRE_FALSE(changes.empty());
	CHECK(changes.back().Step == Graphite::InstallStep::Cancelled);
	CHECK(changes.back().Failure.empty());
	CHECK_FALSE(std::filesystem::exists(fixture.ToolDirectory() / "1.0.0"));
}

TEST_CASE("A directory left behind by an earlier uninstall is replaced by the new install")
{
	InstallerFixture fixture;
	const std::filesystem::path stale = fixture.ToolDirectory() / "1.0.0" / "stale.txt";
	std::filesystem::create_directories(stale.parent_path());
	std::ofstream{stale} << "left by an uninstall whose files were in use";
	ServeRelease(*fixture.Http, "1.0.0", ToolZip("1.0.0"), std::nullopt);

	REQUIRE(fixture.Installer.Install(kGitHubTool));
	const std::vector<Graphite::InstallProgress> changes = fixture.Drain();

	REQUIRE_FALSE(changes.empty());
	CHECK(changes.back().Step == Graphite::InstallStep::Installed);
	CHECK_FALSE(std::filesystem::exists(stale));
	CHECK(std::filesystem::exists(fixture.ToolDirectory() / "1.0.0" / "tool_1.0.0" / "bin" / "tool.exe"));
}

#ifdef CORE_PLATFORM_WINDOWS
TEST_CASE("A version left behind that a running server holds fails the install and stays whole")
{
	InstallerFixture fixture;
	const std::filesystem::path leftover = fixture.ToolDirectory() / "1.0.0" / "tool_1.0.0" / "bin";
	std::filesystem::create_directories(leftover);
	std::ofstream{leftover / "runtime.dll"} << "a library the server loads later";
	std::ofstream{leftover / "tool.exe"} << "the running server";
	ServeRelease(*fixture.Http, "1.0.0", ToolZip("1.0.0"), std::nullopt);

	const HANDLE serverHandle = ::CreateFileW((leftover / "tool.exe").c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
											  OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
	REQUIRE(serverHandle != INVALID_HANDLE_VALUE);
	const Core::ScopeExit closeHandle{[serverHandle] { ::CloseHandle(serverHandle); }};

	REQUIRE(fixture.Installer.Install(kGitHubTool));
	const std::vector<Graphite::InstallProgress> changes = fixture.Drain();

	REQUIRE_FALSE(changes.empty());
	CHECK(changes.back().Step == Graphite::InstallStep::Failed);
	CHECK(changes.back().Failure.find("another program may be holding its files") != std::string::npos);
	CHECK(std::filesystem::exists(leftover / "runtime.dll"));
	CHECK(std::filesystem::exists(leftover / "tool.exe"));
	CHECK_FALSE(std::filesystem::exists(fixture.ToolDirectory() / "1.0.0.tmp"));
	CHECK_FALSE(std::filesystem::exists(Graphite::RecordFileOf(fixture.ToolDirectory())));
}

TEST_CASE("A record that cannot be written leaves no new version behind")
{
	InstallerFixture fixture;
	std::filesystem::create_directories(fixture.ToolDirectory());
	std::ofstream{Graphite::RecordFileOf(fixture.ToolDirectory())} << "{}";
	ServeRelease(*fixture.Http, "1.0.0", ToolZip("1.0.0"), std::nullopt);

	const HANDLE recordHandle = ::CreateFileW(Graphite::RecordFileOf(fixture.ToolDirectory()).c_str(), GENERIC_READ, 0,
											  nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
	REQUIRE(recordHandle != INVALID_HANDLE_VALUE);
	const Core::ScopeExit closeHandle{[recordHandle] { ::CloseHandle(recordHandle); }};

	REQUIRE(fixture.Installer.Install(kGitHubTool));
	const std::vector<Graphite::InstallProgress> changes = fixture.Drain();

	REQUIRE_FALSE(changes.empty());
	CHECK(changes.back().Step == Graphite::InstallStep::Failed);
	CHECK(CountOfStep(changes, Graphite::InstallStep::Extracting) == 1);
	CHECK_FALSE(std::filesystem::exists(fixture.ToolDirectory() / "1.0.0"));
	CHECK_FALSE(std::filesystem::exists(fixture.ToolDirectory() / "1.0.0.tmp"));
}
#endif

TEST_CASE("An install on a worker thread reports through Drain")
{
	TempDirectory temporaryDirectory;
	const Core::Ref<FakeHttpClient> http = Core::CreateRef<FakeHttpClient>();
	const std::string archive = ToolZip("1.0.0");
	ServeRelease(*http, "1.0.0", archive, Sha256Of(archive));
	Core::JobQueue jobs{1};
	Graphite::ServerInstaller installer{http, temporaryDirectory.GetPath() / "servers", kWindowsX64,
										[&jobs](std::function<void()> job) { jobs.Submit(std::move(job)); }};

	REQUIRE(installer.Install(kGitHubTool));

	std::vector<Graphite::InstallProgress> changes;
	const std::chrono::steady_clock::time_point deadline = std::chrono::steady_clock::now() + std::chrono::seconds{10};

	while ((changes.empty() || !Graphite::IsFinished(changes.back().Step)) && std::chrono::steady_clock::now() < deadline)
	{
		installer.Drain(changes);
		std::this_thread::sleep_for(std::chrono::milliseconds{5});
	}

	REQUIRE_FALSE(changes.empty());
	CHECK(changes.back().Step == Graphite::InstallStep::Installed);
}
