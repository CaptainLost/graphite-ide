#include <doctest/doctest.h>

#include "Language/Server/Adapters/ServerAdapter.h"
#include "Language/Server/ServerLaunch.h"

#include "Core/Platform/CurrentPlatform.h"

#include <nlohmann/json.hpp>

#include <optional>
#include <ostream>
#include <string>
#include <string_view>
#include <vector>

namespace {

	std::optional<std::string> ValueOf(const std::vector<Core::EnvironmentVariable>& environment, std::string_view name)
	{
		for (const Core::EnvironmentVariable& variable : environment)
		{
			if (variable.Name == name)
			{
				return variable.Value;
			}
		}

		return std::nullopt;
	}
}
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

TEST_CASE("A launch carries DOTNET_ROOT and the variable of the platform's architecture when it was given one and nothing otherwise")
{
	const Graphite::ServerOverrides noOverrides;
	const Graphite::LaunchPlaces withRuntime{.LeadingArguments = {}, .LogDirectory = {}, .DotnetRoot = "C:\\dotnet"};
	const std::string architectureVariable =
		Core::CurrentPlatform().Machine == Core::Architecture::X64 ? "DOTNET_ROOT_X64" : "DOTNET_ROOT_ARM64";

	const Graphite::ServerLaunch dotnet = Graphite::LaunchFor(*Graphite::FindServer("omnisharp"), "C:\\servers\\OmniSharp.exe", noOverrides, withRuntime);
	const Graphite::ServerLaunch plain = Graphite::LaunchFor(*Graphite::FindServer("clangd"), "C:\\clangd.exe", noOverrides);

	REQUIRE(dotnet.Environment.size() == 2);
	CHECK(dotnet.Environment[0].Name == "DOTNET_ROOT");
	CHECK(dotnet.Environment[0].Value == "C:\\dotnet");
	CHECK(dotnet.Environment[1].Name == architectureVariable);
	CHECK(dotnet.Environment[1].Value == "C:\\dotnet");
	CHECK(plain.Environment.empty());
}

TEST_CASE("Every logs placeholder in an argument is filled in")
{
	const Graphite::ServerOverrides overrides{.ExtraArguments = {"--trace={logs}/one;{logs}/two"}};
	const Graphite::LaunchPlaces places{.LeadingArguments = {}, .LogDirectory = "D:/Logs"};

	const Graphite::ServerLaunch launch = Graphite::LaunchFor(*Graphite::FindServer("clangd"), "C:/clangd.exe", overrides, places);

	CHECK(launch.Arguments.back() == "--trace=D:/Logs/one;D:/Logs/two");
}

TEST_CASE("A launch given a temporary directory points TMP and TEMP at it and one given none leaves both to be inherited")
{
	const Graphite::ServerOverrides noOverrides;
	const Graphite::LaunchPlaces withTemporaryDirectory{.LeadingArguments = {}, .LogDirectory = {}, .TemporaryDirectory = "D:\\Temp\\server-temp\\1"};

	const Graphite::ServerLaunch redirected = Graphite::LaunchFor(*Graphite::FindServer("clangd"), "C:\\clangd.exe", noOverrides, withTemporaryDirectory);
	const Graphite::ServerLaunch inherited = Graphite::LaunchFor(*Graphite::FindServer("clangd"), "C:\\clangd.exe", noOverrides);

	CHECK(ValueOf(redirected.Environment, "TMP") == "D:\\Temp\\server-temp\\1");
	CHECK(ValueOf(redirected.Environment, "TEMP") == "D:\\Temp\\server-temp\\1");
	CHECK_FALSE(ValueOf(inherited.Environment, "TMP").has_value());
	CHECK_FALSE(ValueOf(inherited.Environment, "TEMP").has_value());
}