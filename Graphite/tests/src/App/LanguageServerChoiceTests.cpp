#include <doctest/doctest.h>

#include "App/LanguageServerChoice.h"
#include "Settings/AppSettings.h"
#include "TempDirectory.h"

#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>
#include <string_view>

namespace {

	void WriteText(const std::filesystem::path& path, std::string_view text)
	{
		std::ofstream{path, std::ios::binary} << text;
	}
}

TEST_CASE("C++ reads its server and the user's overrides for it from graphite.json")
{
	const TempDirectory temp;
	const std::filesystem::path file = temp.GetPath() / "graphite.json";

	WriteText(file, R"({
		"languages": { "Cpp": { "language_server": "clangd" } },
		"lsp": { "clangd": {
			"binary": { "path": "D:\\Tools\\clangd.exe", "arguments": ["--log=verbose"] },
			"settings": { "fallbackFlags": ["-std=c++20"] } } }
	})");

	const Graphite::AppSettings settings{file};
	const Graphite::ServerChoice choice = Graphite::ReadServerChoice(settings, Graphite::LanguageId::Cpp);

	REQUIRE(choice.Kind == Graphite::ServerChoiceKind::Chosen);
	CHECK(choice.Server == Graphite::FindServer("clangd"));
	CHECK(choice.Overrides.Binary == std::filesystem::path{L"D:\\Tools\\clangd.exe"});
	CHECK(choice.Overrides.ExtraArguments == std::vector<std::string>{"--log=verbose"});
	CHECK(choice.Overrides.Settings == nlohmann::json{{"fallbackFlags", nlohmann::json::array({"-std=c++20"})}});
	CHECK(choice.Overrides.InitializationOptions.is_null());
}

TEST_CASE("A C++ server path stored under the old key still starts clangd")
{
	const TempDirectory temp;
	Graphite::AppSettings settings{temp.GetPath() / "graphite.json"};

	settings.SetLanguageServerBinary(L"D:\\Old\\clangd.exe");

	CHECK(Graphite::ReadServerChoice(settings, Graphite::LanguageId::Cpp).Overrides.Binary ==
		  std::filesystem::path{L"D:\\Old\\clangd.exe"});
}

TEST_CASE("A path stored for clangd under lsp wins over the old key")
{
	const TempDirectory temp;
	const std::filesystem::path file = temp.GetPath() / "graphite.json";

	WriteText(file, R"({"languageServerBinary": "D:\\Old\\clangd.exe", "lsp": {"clangd": {"binary": {"path": "D:\\New\\clangd.exe"}}}})");

	const Graphite::AppSettings settings{file};

	CHECK(Graphite::ReadServerChoice(settings, Graphite::LanguageId::Cpp).Overrides.Binary ==
		  std::filesystem::path{L"D:\\New\\clangd.exe"});
}

TEST_CASE("C++ stored as none reads as turned off and C# stored as roslyn chooses Roslyn")
{
	const TempDirectory temp;
	const std::filesystem::path file = temp.GetPath() / "graphite.json";

	WriteText(file, R"({"languages": {"Cpp": {"language_server": "none"}, "CSharp": {"language_server": "roslyn"}}})");

	const Graphite::AppSettings settings{file};

	CHECK(Graphite::ReadServerChoice(settings, Graphite::LanguageId::Cpp).Kind == Graphite::ServerChoiceKind::Off);
	const Graphite::ServerChoice choice = Graphite::ReadServerChoice(settings, Graphite::LanguageId::CSharp);
	CHECK(choice.Kind == Graphite::ServerChoiceKind::Chosen);
	CHECK(choice.Server == Graphite::FindServer("roslyn"));
}
