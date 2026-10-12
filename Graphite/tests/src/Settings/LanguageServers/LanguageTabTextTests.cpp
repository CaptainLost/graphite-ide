#include <doctest/doctest.h>

#include "Editor/StatusBar/ServerStatusText.h"
#include "Language/Api/LanguageService.h"
#include "Language/Install/InstallSummary.h"
#include "Language/Install/ServerInstaller.h"
#include "Language/Install/ServerInstalls.h"
#include "Language/Server/ServerCatalog.h"
#include "Language/Server/ServerChoice.h"
#include "Language/Server/ServerCommand.h"
#include "Settings/LanguageServers/LanguageTabText.h"

#include <filesystem>
#include <optional>
#include <ostream>
#include <string>
#include <vector>

TEST_CASE("The first server a language lists is marked recommended")
{
	CHECK(Graphite::ServerOptionLabel(*Graphite::FindServer("roslyn"), true) == "Roslyn (recommended)");
	CHECK(Graphite::ServerOptionLabel(*Graphite::FindServer("csharp-ls"), false) == "csharp-ls");
}

TEST_CASE("The runtime line names the version found or says none was")
{
	const Graphite::ServerRuntime dotnet10{.Kind = Graphite::RuntimeKind::Dotnet, .MinimumMajor = 10};
	const std::vector<Graphite::DotnetInstallation> withTen{
		Graphite::DotnetInstallation{.Executable = "C:/dotnet/dotnet.exe", .RuntimeVersions = {"10.0.3", "8.0.11"}}};
	const std::vector<Graphite::DotnetInstallation> withEight{
		Graphite::DotnetInstallation{.Executable = "C:/dotnet/dotnet.exe", .RuntimeVersions = {"8.0.11"}}};

	CHECK(Graphite::RuntimeLine(dotnet10, withTen) == ".NET 10 runtime \xE2\x80\x94 found 10.0.3");
	CHECK(Graphite::RuntimeFound(dotnet10, withTen));
	CHECK(Graphite::RuntimeLine(dotnet10, withEight) == ".NET 10 runtime \xE2\x80\x94 not found");
	CHECK_FALSE(Graphite::RuntimeFound(dotnet10, withEight));
	CHECK(Graphite::RuntimeLine(Graphite::ServerRuntime{}, withTen).empty());
}

TEST_CASE("The server list shows the choice as the user made it")
{
	CHECK(Graphite::ChoicePreview(Graphite::LanguageId::CSharp, Graphite::ChooseServer(Graphite::LanguageId::CSharp, std::nullopt)) == "Not chosen");
	CHECK(Graphite::ChoicePreview(Graphite::LanguageId::CSharp, Graphite::ChooseServer(Graphite::LanguageId::CSharp, std::string{"none"})) == "None");
	CHECK(Graphite::ChoicePreview(Graphite::LanguageId::CSharp, Graphite::ChooseServer(Graphite::LanguageId::CSharp, std::string{"roslyn"})) ==
		  "Roslyn (recommended)");
	CHECK(Graphite::ChoicePreview(Graphite::LanguageId::CSharp, Graphite::ChooseServer(Graphite::LanguageId::CSharp, std::string{"omnisharp"})) ==
		  "OmniSharp");
	CHECK(Graphite::ChoicePreview(Graphite::LanguageId::Cpp, Graphite::ChooseServer(Graphite::LanguageId::Cpp, std::nullopt)) == "Not chosen");
	CHECK(Graphite::ChoicePreview(Graphite::LanguageId::Cpp, Graphite::ChooseServer(Graphite::LanguageId::Cpp, std::string{"clangd"})) ==
		  "clangd (recommended)");
}

TEST_CASE("Each install action has its button label")
{
	CHECK(std::string{Graphite::InstallActionLabel(Graphite::InstallAction::Install)} == "Install");
	CHECK(std::string{Graphite::InstallActionLabel(Graphite::InstallAction::Cancel)} == "Cancel");
	CHECK(std::string{Graphite::InstallActionLabel(Graphite::InstallAction::CheckForUpdates)} == "Check for updates");
	CHECK(std::string{Graphite::InstallActionLabel(Graphite::InstallAction::Update)} == "Update");
	CHECK(std::string{Graphite::InstallActionLabel(Graphite::InstallAction::Uninstall)} == "Uninstall");
}

TEST_CASE("The server list ends with Custom")
{
	CHECK(std::string{Graphite::CustomChoiceLabel()} == "Custom");
}

TEST_CASE("A server found on PATH is named in the install row with a button to install Graphite's copy")
{
	const Graphite::ServerCommand command{.Program = "C:/app/clangd.exe"};

	CHECK(std::string{Graphite::FoundOnPathTitle()} == "Found on PATH");
	CHECK(Graphite::FoundOnPathNote(command) == "In use: " + Graphite::DescribeCommand(command) + ". Installing puts Graphite's copy ahead of it.");
	CHECK(std::string{Graphite::FoundOnPathActionLabel(Graphite::InstallAction::Install)} == "Install Graphite's copy");
	CHECK(std::string{Graphite::FoundOnPathActionLabel(Graphite::InstallAction::Cancel)} == "Cancel");
}

TEST_CASE("Only a server Graphite has not installed and that runs from elsewhere reads as found on PATH")
{
	const Graphite::InstallInputs notInstalled;
	const std::optional<Graphite::ServerCommand> fromPath = Graphite::ServerCommand{.Program = "C:/app/clangd.exe"};
	const std::optional<Graphite::ServerCommand> fromInstall =
		Graphite::ServerCommand{.Program = "C:/servers/clangd/clangd.exe", .InstalledFile = "C:/servers/clangd/clangd.exe"};
	const std::optional<Graphite::ServerCommand> notRunning;
	const std::filesystem::path noCustomPath;
	const std::filesystem::path customPath{"C:/app/clangd.exe"};

	CHECK(Graphite::IsFoundOnPath(notInstalled, fromPath, noCustomPath));
	CHECK_FALSE(Graphite::IsFoundOnPath(notInstalled, fromInstall, noCustomPath));
	CHECK_FALSE(Graphite::IsFoundOnPath(notInstalled, notRunning, noCustomPath));
	CHECK_FALSE(Graphite::IsFoundOnPath(notInstalled, fromPath, customPath));

	const Graphite::InstallInputs installed{.Installed = Graphite::InstalledVersion{.Version = "22.1.0"}};
	const Graphite::InstallInputs installing{.Running = Graphite::InstallJobKind::Install};
	const Graphite::InstallInputs reading{.HasReadInstalls = false};
	const Graphite::InstallInputs unavailable{.AvailableOnPlatform = false};

	CHECK_FALSE(Graphite::IsFoundOnPath(installed, fromPath, noCustomPath));
	CHECK_FALSE(Graphite::IsFoundOnPath(installing, fromPath, noCustomPath));
	CHECK_FALSE(Graphite::IsFoundOnPath(reading, fromPath, noCustomPath));
	CHECK_FALSE(Graphite::IsFoundOnPath(unavailable, fromPath, noCustomPath));
}

TEST_CASE("The custom path row asks for a program until one is stored and then tells how the server fares")
{
	const std::filesystem::path noPath;
	const std::filesystem::path storedPath{"D:/Tools/clangd.exe"};
	const Graphite::ServerStatus ready{.State = Graphite::ServerState::Ready, .Name = "clangd", .Detail = "clangd version 22.1.0"};
	const Graphite::ServerStatus notFound{.State = Graphite::ServerState::NotFound,
										  .Name = "clangd",
										  .Detail = "the configured path 'D:/Tools/clangd.exe' does not exist"};
	const Graphite::ServerStatus failed{.State = Graphite::ServerState::Failed, .Name = "clangd", .Detail = "D:/Tools/clangd.exe"};

	const Graphite::PathRowText asking = Graphite::CustomPathRowText(noPath, ready);
	CHECK(asking.Description == "Choose the program to run.");
	CHECK(asking.Problem.empty());

	const Graphite::PathRowText running = Graphite::CustomPathRowText(storedPath, ready);
	CHECK(running.Description == Graphite::ServerStatusDetail(ready));
	CHECK(running.Problem.empty());

	const Graphite::PathRowText missing = Graphite::CustomPathRowText(storedPath, notFound);
	CHECK(missing.Description.empty());
	CHECK(missing.Problem == Graphite::ServerStatusDetail(notFound));

	const Graphite::PathRowText broken = Graphite::CustomPathRowText(storedPath, failed);
	CHECK(broken.Description.empty());
	CHECK(broken.Problem == Graphite::ServerStatusDetail(failed));
}
