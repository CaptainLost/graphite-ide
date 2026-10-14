#include <doctest/doctest.h>

#include "Language/Install/PackageNames.h"
#include "Language/Registry/LanguageRegistry.h"
#include "Language/Server/ServerCatalog.h"
#include "Language/Server/ServerDiscovery.h"
#include "Language/Server/ServerPackage.h"
#include "TempDirectory.h"

#include "Core/FileSystem/RelativePath.h"
#include "Core/Platform/CurrentPlatform.h"
#include "Core/Platform/Process.h"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <fstream>
#include <optional>
#include <ostream>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace {

	constexpr Core::PlatformTarget kWindowsX64{.System = Core::OperatingSystem::Windows, .Machine = Core::Architecture::X64};

	void Touch(const std::filesystem::path& path)
	{
		std::filesystem::create_directories(path.parent_path());
		std::ofstream{path} << "";
	}

	constexpr std::array<std::string_view, 3> kSolutionMarkers{"*.sln", "*.slnx", ".git"};

	constexpr Graphite::ServerDefinition kSolutionRooted{.Id = "solution-rooted",
														 .DisplayName = "Solution rooted",
														 .ExecutableName = "no-such-server",
														 .ProtocolLanguageId = "csharp",
														 .RootMarkers = kSolutionMarkers};
}

TEST_CASE("The catalogue knows clangd and C++ recommends it")
{
	const Graphite::ServerDefinition* clangd = Graphite::FindServer("clangd");

	REQUIRE(clangd != nullptr);
	CHECK(clangd->DisplayName == "clangd");
	CHECK(clangd->ExecutableName == "clangd");
	CHECK(clangd->ProtocolLanguageId == "cpp");
	CHECK(clangd->Adapter == Graphite::AdapterKind::Clangd);

	CHECK(Graphite::RecommendedServerFor(Graphite::LanguageId::Cpp) == clangd);
	CHECK(Graphite::RecommendedServerFor(Graphite::LanguageId::PlainText) == nullptr);
	CHECK(Graphite::FindServer("no-such-server") == nullptr);
}

TEST_CASE("Every server a language names is in the catalogue")
{
	for (const Graphite::LanguageId language : Graphite::kAllLanguages)
	{
		for (const std::string_view serverId : Graphite::DefinitionOf(language).Servers)
		{
			CHECK(Graphite::FindServer(serverId) != nullptr);
		}
	}
}

TEST_CASE("clangd starts with background indexing and its preambles on disk")
{
	const std::vector<std::string> arguments = Graphite::DefaultArgumentsOf(*Graphite::FindServer("clangd"));

	CHECK(arguments == std::vector<std::string>{"--background-index", "--pch-storage=disk"});
}

TEST_CASE("RootFor climbs to the nearest directory holding a marker")
{
	TempDirectory temp;

	Touch(temp.GetPath() / "project" / "compile_flags.txt");
	Touch(temp.GetPath() / "project" / "src" / "deep" / "main.cpp");

	const Graphite::ServerDefinition& clangd = *Graphite::FindServer("clangd");

	CHECK(Graphite::RootFor(clangd, temp.GetPath() / "project" / "src" / "deep" / "main.cpp",
							temp.GetPath()) == temp.GetPath() / "project");
}

TEST_CASE("RootFor treats a compile database in build/ as a project marker")
{
	TempDirectory temp;

	Touch(temp.GetPath() / "project" / "build" / "compile_commands.json");
	Touch(temp.GetPath() / "project" / "src" / "main.cpp");

	const Graphite::ServerDefinition& clangd = *Graphite::FindServer("clangd");

	CHECK(Graphite::RootFor(clangd, temp.GetPath() / "project" / "src" / "main.cpp", {}) ==
		  temp.GetPath() / "project");
	CHECK(Graphite::IsProjectRoot(temp.GetPath() / "project"));
}

TEST_CASE("RootFor falls back when no marker is anywhere above the file")
{
	TempDirectory temp;

	Touch(temp.GetPath() / "loose" / "main.cpp");

	const Graphite::ServerDefinition& clangd = *Graphite::FindServer("clangd");

	CHECK(Graphite::RootFor(clangd, temp.GetPath() / "loose" / "main.cpp", temp.GetPath()) ==
		  temp.GetPath());
}

TEST_CASE("FindServerBinary prefers a configured path that exists")
{
	TempDirectory temp;
	const std::filesystem::path configured = temp.GetPath() / "clangd.exe";

	Touch(configured);

	const Core::Result<std::filesystem::path> found = Graphite::FindServerBinary("clangd", configured);

	REQUIRE(found);
	CHECK(found.GetValue() == configured);
}

TEST_CASE("FindServerBinary finds a server in a directory on PATH whose name is outside the ANSI code page")
{
	namespace fs = std::filesystem;
	const fs::path directory = fs::temp_directory_path() / L"graphite-中文-\U0001F600-server";
	std::error_code status;
	fs::create_directories(directory, status);
	REQUIRE_FALSE(status);

	Touch(directory / L"graphite-wide-server-probe.exe");

	const std::optional<fs::path> original = Core::EnvironmentPath(L"PATH");
	REQUIRE(original.has_value());
	REQUIRE(_wputenv_s(L"PATH", directory.wstring().c_str()) == 0);

	const std::filesystem::path noConfiguredPath;
	const Core::Result<std::filesystem::path> found = Graphite::FindServerBinary("graphite-wide-server-probe", noConfiguredPath);

	// WARNING: PATH is restored before asserting so a failure cannot leave later tests with a broken PATH.
	const errno_t restored = _wputenv_s(L"PATH", original->wstring().c_str());
	fs::remove_all(directory, status);

	CHECK(restored == 0);
	REQUIRE(found);
	CHECK(found.GetValue() == directory / L"graphite-wide-server-probe.exe");
}

TEST_CASE("FindServerBinary reports not found rather than an empty path")
{
	const std::filesystem::path noConfiguredPath;
	const Core::Result<std::filesystem::path> found = Graphite::FindServerBinary("no-such-server-8a3f", noConfiguredPath);

	CHECK_FALSE(found);
	CHECK(found.GetError().Code == Core::ErrorCode::NotFound);
}

TEST_CASE("clangd comes from its GitHub releases as a zip with the server inside")
{
	const Graphite::ServerDefinition& clangd = *Graphite::FindServer("clangd");
	const Graphite::GitHubReleasePackage* release = std::get_if<Graphite::GitHubReleasePackage>(&clangd.Package);

	REQUIRE(release != nullptr);
	CHECK(release->Repository == "clangd/clangd");

	const Graphite::GitHubAsset* windows = Graphite::FindAssetFor(*release, kWindowsX64);

	REQUIRE(windows != nullptr);
	CHECK(windows->AssetTemplate == "clangd-{os}-{version}.zip");
	CHECK(windows->Archive == Graphite::ArchiveKind::Zip);
	CHECK(windows->ExecutableTemplate == "clangd_{version}/bin/clangd{exe}");
}

TEST_CASE("Every server installs on Windows x64 and every template it uses expands")
{
	for (const Graphite::ServerDefinition& server : Graphite::AllServers())
	{
		INFO(std::string{server.Id});
		CHECK(Graphite::IsSafeDirectoryName(server.Id));
		CHECK(Graphite::IsAvailableOn(server.Package, kWindowsX64));

		if (const Graphite::GitHubReleasePackage* release = std::get_if<Graphite::GitHubReleasePackage>(&server.Package))
		{
			for (const Graphite::GitHubAsset& asset : release->Assets)
			{
				const Graphite::TemplateValues values{.Version = "1.0.0", .Platform = asset.Platform};
				const Core::Result<std::string> executable = Graphite::ExpandPackageTemplate(asset.ExecutableTemplate, values);

				CHECK(Graphite::ExpandPackageTemplate(asset.AssetTemplate, values));
				REQUIRE(executable);
				CHECK(Core::IsSafeRelativePath(executable.GetValue()));
			}
		}

		if (const Graphite::NuGetToolPackage* tool = std::get_if<Graphite::NuGetToolPackage>(&server.Package))
		{
			CHECK(Graphite::ExpandPackageTemplate(tool->PackageIdTemplate, {.Version = "", .Platform = kWindowsX64}));
		}
	}
}

TEST_CASE("A marker written as a star and an extension matches a file with that extension in any case")
{
	TempDirectory temp;
	Touch(temp.GetPath() / "proj" / "Demo.SLNX");
	Touch(temp.GetPath() / "proj" / "Demo" / "Program.cs");

	const std::filesystem::path root =
		Graphite::RootFor(kSolutionRooted, temp.GetPath() / "proj" / "Demo" / "Program.cs", temp.GetPath());

	CHECK(root == temp.GetPath() / "proj");
}

TEST_CASE("A directory named like a solution is not a solution")
{
	TempDirectory temp;
	std::filesystem::create_directories(temp.GetPath() / "proj" / "fake.sln");
	Touch(temp.GetPath() / "proj" / "Demo" / "Program.cs");

	const std::filesystem::path fallback = temp.GetPath() / "fallback";
	const std::filesystem::path root =
		Graphite::RootFor(kSolutionRooted, temp.GetPath() / "proj" / "Demo" / "Program.cs", fallback);

	CHECK(root == fallback);
}

TEST_CASE("A literal marker listed beside patterns still counts")
{
	TempDirectory temp;
	std::filesystem::create_directories(temp.GetPath() / "repo" / ".git");
	Touch(temp.GetPath() / "repo" / "src" / "Program.cs");

	CHECK(Graphite::DirectoryHoldsAnyMarker(temp.GetPath() / "repo", kSolutionMarkers));
	CHECK_FALSE(Graphite::DirectoryHoldsAnyMarker(temp.GetPath() / "repo" / "src", kSolutionMarkers));
}

TEST_CASE("A server is named by its display name and an unknown one by its id")
{
	CHECK(Graphite::ServerDisplayName("roslyn") == "Roslyn");
	CHECK(Graphite::ServerDisplayName("omnisharp") == "OmniSharp");
	CHECK(Graphite::ServerDisplayName("no-such-server") == "no-such-server");
}

TEST_CASE("A folder holding only a solution file is not a nested project root")
{
	TempDirectory temp;
	Touch(temp.GetPath() / "sub" / "Tool.sln");

	CHECK_FALSE(Graphite::IsProjectRoot(temp.GetPath() / "sub"));
}

TEST_CASE("C# offers Roslyn first then csharp-ls and OmniSharp")
{
	const std::span<const std::string_view> servers = Graphite::DefinitionOf(Graphite::LanguageId::CSharp).Servers;

	REQUIRE(servers.size() == 3);
	CHECK(servers[0] == "roslyn");
	CHECK(servers[1] == "csharp-ls");
	CHECK(servers[2] == "omnisharp");
	CHECK(Graphite::RecommendedServerFor(Graphite::LanguageId::CSharp) == Graphite::FindServer("roslyn"));
}

TEST_CASE("Each C# server speaks csharp from a solution root and needs .NET 10")
{
	for (const std::string_view serverId : {std::string_view{"roslyn"}, std::string_view{"csharp-ls"}, std::string_view{"omnisharp"}})
	{
		INFO(std::string{serverId});
		const Graphite::ServerDefinition* server = Graphite::FindServer(serverId);
		REQUIRE(server != nullptr);

		CHECK(server->ProtocolLanguageId == "csharp");
		CHECK(server->Runtime.Kind == Graphite::RuntimeKind::Dotnet);
		CHECK(server->Runtime.MinimumMajor == 10);
		CHECK(std::find(server->RootMarkers.begin(), server->RootMarkers.end(), std::string_view{"*.sln"}) != server->RootMarkers.end());
		CHECK(std::find(server->RootMarkers.begin(), server->RootMarkers.end(), std::string_view{"*.slnx"}) != server->RootMarkers.end());
	}

	CHECK(Graphite::FindServer("clangd")->Runtime.Kind == Graphite::RuntimeKind::None);
}

TEST_CASE("Roslyn and csharp-ls come from NuGet and OmniSharp from its GitHub releases")
{
	const Graphite::NuGetToolPackage* roslyn = std::get_if<Graphite::NuGetToolPackage>(&Graphite::FindServer("roslyn")->Package);
	REQUIRE(roslyn != nullptr);
	CHECK(roslyn->PackageIdTemplate == "roslyn-language-server.{rid}");
	CHECK(roslyn->AllowPrerelease);

	const Graphite::NuGetToolPackage* csharpLs = std::get_if<Graphite::NuGetToolPackage>(&Graphite::FindServer("csharp-ls")->Package);
	REQUIRE(csharpLs != nullptr);
	CHECK(csharpLs->PackageIdTemplate == "csharp-ls");
	CHECK_FALSE(csharpLs->AllowPrerelease);

	const Graphite::GitHubReleasePackage* omniSharp =
		std::get_if<Graphite::GitHubReleasePackage>(&Graphite::FindServer("omnisharp")->Package);
	REQUIRE(omniSharp != nullptr);
	CHECK(omniSharp->Repository == "OmniSharp/omnisharp-roslyn");
	const Graphite::GitHubAsset* windows = Graphite::FindAssetFor(*omniSharp, kWindowsX64);
	REQUIRE(windows != nullptr);
	CHECK(windows->AssetTemplate == "omnisharp-{rid}.zip");
	CHECK(windows->ExecutableTemplate == "OmniSharp{exe}");

	const std::vector<std::string> roslynArguments = Graphite::DefaultArgumentsOf(*Graphite::FindServer("roslyn"));
	CHECK(roslynArguments == std::vector<std::string>{"--stdio", "--logLevel", "Information", "--extensionLogDirectory", "{logs}/roslyn"});
	CHECK(Graphite::DefaultArgumentsOf(*Graphite::FindServer("omnisharp")) == std::vector<std::string>{"-lsp"});
}

TEST_CASE("Every server describes itself in one line")
{
	for (const Graphite::ServerDefinition& server : Graphite::AllServers())
	{
		INFO(std::string{server.Id});
		CHECK_FALSE(server.Description.empty());
		CHECK(server.Description.back() == '.');
	}

	CHECK(Graphite::FindServer("roslyn")->Description == "Microsoft's official C# language server.");
}

TEST_CASE("A server beside the application is not found")
{
	const Core::Result<std::filesystem::path> directory = Core::ExecutableDirectory();
	REQUIRE(directory);
	const std::filesystem::path beside = directory.GetValue() / "graphite-beside-probe-5c1e.exe";

	Touch(beside);
	const std::filesystem::path noConfiguredPath;
	const Core::Result<std::filesystem::path> found = Graphite::FindServerBinary("graphite-beside-probe-5c1e", noConfiguredPath);
	std::error_code removeError;
	std::filesystem::remove(beside, removeError);

	CHECK_FALSE(found);
}
