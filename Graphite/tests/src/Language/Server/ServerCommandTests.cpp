#include <doctest/doctest.h>

#include "InstalledServers.h"
#include "Language/Server/ServerCatalog.h"
#include "Language/Server/ServerCommand.h"
#include "TempDirectory.h"

#include "Core/FileSystem/FileSystem.h"

#include <filesystem>
#include <ostream>
#include <string>
#include <vector>

namespace {

	constexpr Graphite::ServerDefinition kNowhere{.Id = "nowhere",
												  .DisplayName = "Nowhere",
												  .ExecutableName = "graphite-test-server-found-nowhere",
												  .ProtocolLanguageId = "plaintext"};

	Graphite::ServerCommandPlaces WithDotnet(const std::filesystem::path& serversDirectory, std::string runtimeVersion)
	{
		const std::vector<Graphite::DotnetInstallation> installations{
			Graphite::DotnetInstallation{.Executable = "C:/dotnet/dotnet.exe", .RuntimeVersions = {std::move(runtimeVersion)}}};

		return Graphite::ServerCommandPlaces{.ServersDirectory = serversDirectory, .ListDotnet = [installations] { return installations; }};
	}
}

TEST_CASE("A configured path that does not exist is NotFound naming it")
{
	TempDirectory temp;
	const std::filesystem::path configured = temp.GetPath() / "gone" / "clangd.exe";

	const Graphite::ServerCommand command =
		Graphite::FindServerCommand(*Graphite::FindServer("clangd"), configured, Graphite::ServerCommandPlaces{});

	CHECK(command.State == Graphite::ServerState::NotFound);
	CHECK(command.Detail.find(Core::ToUtf8(configured)) != std::string::npos);
	CHECK(command.Program.empty());
}

TEST_CASE("A configured server runs as configured and a configured assembly runs through dotnet")
{
	TempDirectory temp;
	const std::filesystem::path clangd = temp.MakeFile("clangd.exe", "");
	const std::filesystem::path assembly = temp.MakeFile("CSharpLanguageServer.dll", "");

	const Graphite::ServerCommand native = Graphite::FindServerCommand(*Graphite::FindServer("clangd"), clangd, {});
	CHECK(native.State == Graphite::ServerState::Starting);
	CHECK(native.Program == clangd);
	CHECK(native.LeadingArguments.empty());
	CHECK(native.InstalledFile.empty());

	const Graphite::ServerCommand managed =
		Graphite::FindServerCommand(*Graphite::FindServer("csharp-ls"), assembly, WithDotnet({}, "10.0.10"));
	CHECK(managed.State == Graphite::ServerState::Starting);
	CHECK(managed.Program == std::filesystem::path{"C:/dotnet/dotnet.exe"});
	CHECK(managed.LeadingArguments == std::vector<std::string>{Core::ToUtf8(assembly)});
}

TEST_CASE("A server Graphite installed runs from its install through the runner its record names")
{
	TempDirectory temp;
	const std::filesystem::path version = InstallCsharpLsIn(temp.GetPath() / "servers");
	const std::filesystem::path assembly = version / kCsharpLsAssembly;

	const Graphite::ServerCommand command =
		Graphite::FindServerCommand(*Graphite::FindServer("csharp-ls"), {}, WithDotnet(temp.GetPath() / "servers", "10.0.10"));

	CHECK(command.State == Graphite::ServerState::Starting);
	CHECK(command.Program == std::filesystem::path{"C:/dotnet/dotnet.exe"});
	CHECK(command.LeadingArguments == std::vector<std::string>{Core::ToUtf8(assembly)});
	CHECK(command.InstalledFile == assembly);
	CHECK(Graphite::DescribeCommand(command) == "C:/dotnet/dotnet.exe " + Core::ToUtf8(assembly));
}

TEST_CASE("A .NET server is told where the runtime it was checked against lives")
{
	TempDirectory temp;
	InstallCsharpLsIn(temp.GetPath() / "servers");

	const Graphite::ServerCommand csharpLs =
		Graphite::FindServerCommand(*Graphite::FindServer("csharp-ls"), {}, WithDotnet(temp.GetPath() / "servers", "10.0.10"));

	REQUIRE(csharpLs.State == Graphite::ServerState::Starting);
	CHECK(csharpLs.DotnetRoot == std::filesystem::path{"C:/dotnet"});
}

TEST_CASE("A server that needs no runtime is told of none")
{
	TempDirectory temp;
	const std::filesystem::path clangd = temp.MakeFile("clangd.exe", "");

	const Graphite::ServerCommand command = Graphite::FindServerCommand(*Graphite::FindServer("clangd"), clangd, Graphite::ServerCommandPlaces{});

	REQUIRE(command.State == Graphite::ServerState::Starting);
	CHECK(command.DotnetRoot.empty());
}

TEST_CASE("A .NET server without a new enough runtime is MissingRuntime naming the version")
{
	TempDirectory temp;
	InstallCsharpLsIn(temp.GetPath() / "servers");

	const Graphite::ServerCommand withOldRuntime =
		Graphite::FindServerCommand(*Graphite::FindServer("csharp-ls"), {}, WithDotnet(temp.GetPath() / "servers", "8.0.11"));
	CHECK(withOldRuntime.State == Graphite::ServerState::MissingRuntime);
	CHECK(withOldRuntime.Detail == "needs .NET 10");

	const Graphite::ServerCommandPlaces noDotnet{.ServersDirectory = temp.GetPath() / "servers", .ListDotnet = {}};
	CHECK(Graphite::FindServerCommand(*Graphite::FindServer("csharp-ls"), {}, noDotnet).State == Graphite::ServerState::MissingRuntime);
}

TEST_CASE("A .NET server configured as a program of its own is told where its runtime lives")
{
	TempDirectory temp;
	const std::filesystem::path omniSharp = temp.MakeFile("OmniSharp.exe", "");

	const Graphite::ServerCommand command = Graphite::FindServerCommand(*Graphite::FindServer("omnisharp"), omniSharp, WithDotnet({}, "10.0.10"));

	REQUIRE(command.State == Graphite::ServerState::Starting);
	CHECK(command.Program == omniSharp);
	CHECK(command.LeadingArguments.empty());
	CHECK(command.DotnetRoot == std::filesystem::path{"C:/dotnet"});
}

TEST_CASE("An assembly configured for a server that names no runtime runs through dotnet")
{
	TempDirectory temp;
	const std::filesystem::path assembly = temp.MakeFile("clangd.dll", "");

	const Graphite::ServerCommand withDotnet = Graphite::FindServerCommand(*Graphite::FindServer("clangd"), assembly, WithDotnet({}, "8.0.11"));
	const Graphite::ServerCommand withoutDotnet = Graphite::FindServerCommand(*Graphite::FindServer("clangd"), assembly, Graphite::ServerCommandPlaces{});

	REQUIRE(withDotnet.State == Graphite::ServerState::Starting);
	CHECK(withDotnet.Program == std::filesystem::path{"C:/dotnet/dotnet.exe"});
	CHECK(withDotnet.LeadingArguments == std::vector<std::string>{Core::ToUtf8(assembly)});
	CHECK(withoutDotnet.State == Graphite::ServerState::MissingRuntime);
	CHECK(withoutDotnet.Detail == "needs .NET");
}

TEST_CASE("Only a command that runs on .NET lists the .NET installations")
{
	TempDirectory temp;
	const std::filesystem::path clangd = temp.MakeFile("clangd.exe", "");
	std::size_t listings = 0;
	const Graphite::ServerCommandPlaces counted{.ServersDirectory = {}, .ListDotnet = [&listings] {
													++listings;
													return std::vector<Graphite::DotnetInstallation>{};
												}};

	const Graphite::ServerCommand command = Graphite::FindServerCommand(*Graphite::FindServer("clangd"), clangd, counted);

	REQUIRE(command.State == Graphite::ServerState::Starting);
	CHECK(listings == 0);
}

TEST_CASE("A server found nowhere is NotInstalled and says where it was looked for")
{
	TempDirectory temp;
	const Graphite::ServerCommandPlaces installs{.ServersDirectory = temp.GetPath() / "servers", .ListDotnet = {}};

	const Graphite::ServerCommand withServersDirectory = Graphite::FindServerCommand(kNowhere, {}, installs);
	const Graphite::ServerCommand withoutServersDirectory = Graphite::FindServerCommand(kNowhere, {}, Graphite::ServerCommandPlaces{});

	CHECK(withServersDirectory.State == Graphite::ServerState::NotInstalled);
	CHECK(withServersDirectory.Detail == "it is not in Graphite's servers folder or on PATH");
	CHECK(withoutServersDirectory.State == Graphite::ServerState::NotInstalled);
	CHECK(withoutServersDirectory.Detail == "it is not on PATH");
}

TEST_CASE("A configured path naming a folder is NotFound saying it is a folder")
{
	TempDirectory temp;
	const std::filesystem::path folder = temp.GetPath() / "clangd";
	std::filesystem::create_directories(folder);

	const Graphite::ServerCommand command = Graphite::FindServerCommand(*Graphite::FindServer("clangd"), folder, Graphite::ServerCommandPlaces{});

	CHECK(command.State == Graphite::ServerState::NotFound);
	CHECK(command.Detail == "the configured path '" + Core::ToUtf8(folder) + "' is a folder, not a program");
}
