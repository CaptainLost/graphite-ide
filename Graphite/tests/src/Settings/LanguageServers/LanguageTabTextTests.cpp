#include <doctest/doctest.h>

#include "Language/Install/InstallSummary.h"
#include "Language/Server/ServerCatalog.h"
#include "Language/Server/ServerChoice.h"
#include "Language/Server/ServerCommand.h"
#include "Settings/LanguageServers/LanguageTabText.h"

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
	CHECK(Graphite::ChoicePreview(Graphite::LanguageId::Cpp, Graphite::ChooseServer(Graphite::LanguageId::Cpp, std::nullopt)) == "clangd (recommended)");
}

TEST_CASE("Each install action has its button label")
{
	CHECK(std::string{Graphite::InstallActionLabel(Graphite::InstallAction::Install)} == "Install");
	CHECK(std::string{Graphite::InstallActionLabel(Graphite::InstallAction::Cancel)} == "Cancel");
	CHECK(std::string{Graphite::InstallActionLabel(Graphite::InstallAction::CheckForUpdates)} == "Check for updates");
	CHECK(std::string{Graphite::InstallActionLabel(Graphite::InstallAction::Update)} == "Update");
	CHECK(std::string{Graphite::InstallActionLabel(Graphite::InstallAction::Uninstall)} == "Uninstall");
}

TEST_CASE("A server running from Graphite's install needs no note")
{
	const Graphite::ServerCommand command{.Program = "C:/servers/clangd/clangd.exe", .InstalledFile = "C:/servers/clangd/clangd.exe"};

	CHECK(Graphite::RunningElsewhereNote(command, false).empty());
	CHECK(Graphite::RunningElsewhereNote(command, true).empty());
}

TEST_CASE("A server running from a custom path says so")
{
	const Graphite::ServerCommand command{.Program = "C:/tools/clangd.exe"};

	CHECK(Graphite::RunningElsewhereNote(command, true) == "In use: " + Graphite::DescribeCommand(command) + ", from the custom path.");
}

TEST_CASE("A server found outside Graphite's install says where installing would put its own copy")
{
	const Graphite::ServerCommand command{.Program = "C:/app/clangd.exe"};

	CHECK(Graphite::RunningElsewhereNote(command, false) ==
		  "In use: " + Graphite::DescribeCommand(command) + ", found outside Graphite's install. Installing puts Graphite's copy ahead of it.");
}
