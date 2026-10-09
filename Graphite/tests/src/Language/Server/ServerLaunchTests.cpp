#include <doctest/doctest.h>

#include "Language/Server/Adapters/ServerAdapter.h"
#include "Language/Server/ServerLaunch.h"

#include <nlohmann/json.hpp>

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
