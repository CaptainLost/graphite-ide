#include <doctest/doctest.h>

#include "Language/Server/Adapters/ServerAdapter.h"
#include "Language/Server/ServerLaunch.h"

#include <nlohmann/json.hpp>

#include <ostream>
#include <string>
#include <vector>

TEST_CASE("A launch runs the server's own arguments first and the user's after them")
{
	const Graphite::ServerOverrides overrides{.Binary = {}, .ExtraArguments = {"--log=verbose"}, .InitializationOptions = nullptr, .Settings = nullptr};

	const Graphite::ServerLaunch launch = Graphite::LaunchFor(*Graphite::FindServer("clangd"), "C:\\clangd.exe", overrides);

	CHECK(launch.Arguments == std::vector<std::string>{"--background-index", "--pch-storage=disk", "--log=verbose"});
	CHECK(launch.Binary == std::filesystem::path{L"C:\\clangd.exe"});
}

TEST_CASE("A launch carries the adapter its server names and the user's options and settings")
{
	const nlohmann::json options{{"fallbackFlags", nlohmann::json::array({"-std=c++20"})}};
	const nlohmann::json settings{{"clangd", {{"arguments", 1}}}};
	const Graphite::ServerOverrides overrides{.Binary = {}, .ExtraArguments = {}, .InitializationOptions = options, .Settings = settings};

	const Graphite::ServerLaunch launch = Graphite::LaunchFor(*Graphite::FindServer("clangd"), "C:\\clangd.exe", overrides);

	CHECK(launch.Server == Graphite::FindServer("clangd"));
	CHECK(launch.Adapter == &Graphite::AdapterOf(Graphite::AdapterKind::Clangd));
	CHECK(launch.InitializationOptions == options);
	CHECK(launch.Settings == settings);
}

TEST_CASE("A launch puts the runner's arguments first and fills in the log directory")
{
	const Graphite::LaunchPlaces places{.LeadingArguments = {"C:\\servers\\roslyn.dll"}, .LogDirectory = "D:\\Logs"};

	const Graphite::ServerLaunch launch = Graphite::LaunchFor(*Graphite::FindServer("roslyn"), "C:\\dotnet\\dotnet.exe", {}, places);

	CHECK(launch.Binary == std::filesystem::path{L"C:\\dotnet\\dotnet.exe"});
	CHECK(launch.Arguments == std::vector<std::string>{"C:\\servers\\roslyn.dll", "--stdio", "--logLevel", "Information",
														"--extensionLogDirectory", "D:\\Logs/roslyn"});
}

TEST_CASE("A launch describes the program with the arguments that pick what it runs")
{
	const Graphite::ServerOverrides noOverrides;
	const Graphite::LaunchPlaces places{.LeadingArguments = {"C:\\servers\\csharp-ls\\CSharpLanguageServer.dll"}, .LogDirectory = {}};

	const Graphite::ServerLaunch launch = Graphite::LaunchFor(*Graphite::FindServer("csharp-ls"), "C:\\dotnet\\dotnet.exe", noOverrides, places);

	CHECK(launch.Description == "C:\\dotnet\\dotnet.exe C:\\servers\\csharp-ls\\CSharpLanguageServer.dll");
}

TEST_CASE("A launch carries DOTNET_ROOT when it was given one and nothing otherwise")
{
	const Graphite::ServerOverrides noOverrides;
	const Graphite::LaunchPlaces withRuntime{.LeadingArguments = {}, .LogDirectory = {}, .DotnetRoot = "C:\\dotnet"};

	const Graphite::ServerLaunch dotnet = Graphite::LaunchFor(*Graphite::FindServer("omnisharp"), "C:\\servers\\OmniSharp.exe", noOverrides, withRuntime);
	const Graphite::ServerLaunch plain = Graphite::LaunchFor(*Graphite::FindServer("clangd"), "C:\\clangd.exe", noOverrides);

	REQUIRE(dotnet.Environment.size() == 1);
	CHECK(dotnet.Environment[0].Name == "DOTNET_ROOT");
	CHECK(dotnet.Environment[0].Value == "C:\\dotnet");
	CHECK(plain.Environment.empty());
}
