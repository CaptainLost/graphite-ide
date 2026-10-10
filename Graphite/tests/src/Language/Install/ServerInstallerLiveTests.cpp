#include <doctest/doctest.h>

#include "Language/Install/InstalledServer.h"
#include "Language/Install/ServerInstaller.h"
#include "Language/Server/ServerCatalog.h"

#include "Core/Network/HttpClient.h"
#include "Core/Platform/CurrentPlatform.h"

#include "TempDirectory.h"

#include <chrono>
#include <filesystem>
#include <format>
#include <functional>
#include <optional>
#include <ostream>
#include <string>
#include <vector>

namespace {

	struct LiveInstall
	{
		std::vector<Graphite::InstallProgress> Changes;
		std::optional<Graphite::InstalledServer> Installed;
	};

	LiveInstall InstallForReal(const Graphite::ServerDefinition& server, const std::filesystem::path& servers)
	{
		Core::Result<Core::Scope<Core::HttpClient>> created = Core::CreateHttpClient("Graphite-tests");
		REQUIRE(created);
		const Core::Ref<Core::HttpClient> http{std::move(created).GetValue()};
		Graphite::ServerInstaller installer{http, servers, Core::CurrentPlatform(), [](std::function<void()> job) { job(); }};
		const std::chrono::steady_clock::time_point started = std::chrono::steady_clock::now();

		REQUIRE(installer.Install(server));

		LiveInstall install;
		installer.Drain(install.Changes);
		const std::chrono::seconds took =
			std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - started);
		MESSAGE(std::format("{} installed in {} s", server.Id, took.count()));
		install.Installed = Graphite::FindInstalledServer(Graphite::ServerDirectoryOf(servers, server.Id));
		return install;
	}
}

TEST_CASE("clangd installs from its GitHub releases" * doctest::skip())
{
	TempDirectory temporaryDirectory;
	const LiveInstall install = InstallForReal(*Graphite::FindServer("clangd"), temporaryDirectory.GetPath());

	REQUIRE_FALSE(install.Changes.empty());
	INFO(install.Changes.back().Failure);
	CHECK(install.Changes.back().Step == Graphite::InstallStep::Installed);
	CHECK(install.Changes.back().HasChecksum);
	REQUIRE(install.Installed.has_value());
	CHECK(install.Installed->Command.filename() == "clangd.exe");
	CHECK(std::filesystem::exists(install.Installed->Command.parent_path().parent_path() / "lib" / "clang"));
}

TEST_CASE("csharp-ls installs from NuGet as a tool run through dotnet" * doctest::skip())
{
	constexpr Graphite::ServerDefinition kCsharpLs{
		.Id = "csharp-ls",
		.DisplayName = "csharp-ls",
		.ExecutableName = "csharp-ls",
		.ProtocolLanguageId = "csharp",
		.Package = Graphite::NuGetToolPackage{.PackageIdTemplate = "csharp-ls", .AllowPrerelease = false}};
	TempDirectory temporaryDirectory;
	const LiveInstall install = InstallForReal(kCsharpLs, temporaryDirectory.GetPath());

	REQUIRE_FALSE(install.Changes.empty());
	INFO(install.Changes.back().Failure);
	CHECK(install.Changes.back().Step == Graphite::InstallStep::Installed);
	CHECK(install.Changes.back().HasChecksum);
	REQUIRE(install.Installed.has_value());
	CHECK(install.Installed->Record.Command.Runner == Graphite::LaunchRunner::Dotnet);
	CHECK(install.Installed->Command.filename() == "CSharpLanguageServer.dll");
}

TEST_CASE("Roslyn installs from NuGet as a self-contained executable" * doctest::skip())
{
	constexpr Graphite::ServerDefinition kRoslyn{
		.Id = "roslyn",
		.DisplayName = "Roslyn",
		.ExecutableName = "roslyn-language-server",
		.ProtocolLanguageId = "csharp",
		.Package = Graphite::NuGetToolPackage{.PackageIdTemplate = "roslyn-language-server.{rid}", .AllowPrerelease = true}};
	TempDirectory temporaryDirectory;
	const LiveInstall install = InstallForReal(kRoslyn, temporaryDirectory.GetPath());

	REQUIRE_FALSE(install.Changes.empty());
	INFO(install.Changes.back().Failure);
	CHECK(install.Changes.back().Step == Graphite::InstallStep::Installed);
	CHECK(install.Changes.back().HasChecksum);
	REQUIRE(install.Installed.has_value());
	CHECK(install.Installed->Record.Command.Runner == Graphite::LaunchRunner::Executable);
	CHECK(install.Installed->Command.filename() == "roslyn-language-server.exe");
}
