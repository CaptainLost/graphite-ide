#include <doctest/doctest.h>

#include "Language/Install/InstalledServer.h"
#include "Language/Install/ServerInstaller.h"
#include "Language/Server/LanguageServices.h"
#include "Language/Server/ServerCatalog.h"
#include "Language/Server/ServerChoice.h"
#include "TempDirectory.h"

#include "Core/Network/HttpClient.h"
#include "Core/Platform/CurrentPlatform.h"

#include <chrono>
#include <filesystem>
#include <format>
#include <functional>
#include <optional>
#include <ostream>
#include <string>
#include <thread>
#include <vector>

namespace {

	constexpr std::chrono::seconds kLiveTimeout{120};

	constexpr const char* kSolution = R"(<Solution>
  <Project Path="Demo/Demo.csproj" />
</Solution>
)";

	constexpr const char* kProject = R"(<Project Sdk="Microsoft.NET.Sdk">
  <PropertyGroup>
    <OutputType>Exe</OutputType>
    <TargetFramework>net10.0</TargetFramework>
  </PropertyGroup>
</Project>
)";

	// Line 2, column 77 is the "Hello" of Greeter.Hello; its definition is line 1, column 44.
	constexpr const char* kProgram = "namespace Demo;\n"
									 "static class Greeter { public static string Hello(string name) => $\"Hello {name}\"; }\n"
									 "static class Program { static void Main() { System.Console.WriteLine(Greeter.Hello(\"x\")); } }\n";

	void InstallForReal(std::string_view serverId, const std::filesystem::path& servers)
	{
		Core::Result<Core::Scope<Core::HttpClient>> created = Core::CreateHttpClient("Graphite-tests");
		REQUIRE(created);
		const Core::Ref<Core::HttpClient> http{std::move(created).GetValue()};
		Graphite::ServerInstaller installer{http, servers, Core::CurrentPlatform(), [](std::function<void()> job) { job(); }};

		REQUIRE(installer.Install(*Graphite::FindServer(serverId)));

		std::vector<Graphite::InstallProgress> changes;
		installer.Drain(changes);
		REQUIRE_FALSE(changes.empty());
		INFO(changes.back().Failure);
		REQUIRE(changes.back().Step == Graphite::InstallStep::Installed);
	}

	bool WaitUntilReady(Graphite::LanguageServices& services, const std::filesystem::path& source)
	{
		const std::chrono::steady_clock::time_point deadline = std::chrono::steady_clock::now() + kLiveTimeout;

		while (std::chrono::steady_clock::now() < deadline)
		{
			services.Poll();

			const Graphite::ServerStatus status = services.StatusFor(source, Graphite::LanguageId::CSharp);

			if (status.State == Graphite::ServerState::Ready)
			{
				return true;
			}

			if (status.State == Graphite::ServerState::Failed || status.State == Graphite::ServerState::NotInstalled ||
				status.State == Graphite::ServerState::MissingRuntime)
			{
				MESSAGE(std::format("stopped waiting at {}", status.Detail));
				return false;
			}

			std::this_thread::sleep_for(std::chrono::milliseconds{100});
		}

		return false;
	}

	// A server still loading may answer a definition with nothing, so it is asked again until it names something.
	std::optional<Graphite::Location> DefinitionOfHello(Graphite::LanguageServices& services, const std::filesystem::path& source)
	{
		const std::chrono::steady_clock::time_point deadline = std::chrono::steady_clock::now() + kLiveTimeout;
		const Graphite::FilePosition hello{.Path = source, .Line = 2, .Column = Graphite::Utf16Column{77}};

		while (std::chrono::steady_clock::now() < deadline)
		{
			const Graphite::LanguageServices::Ticket ticket = services.AskDefinition(source, Graphite::LanguageId::CSharp, hello);
			std::vector<Graphite::LanguageServices::Answer> answers;

			while (answers.empty() && std::chrono::steady_clock::now() < deadline)
			{
				std::this_thread::sleep_for(std::chrono::milliseconds{100});
				services.Poll();
				services.TakeAnswers(answers);
			}

			for (const Graphite::LanguageServices::Answer& answer : answers)
			{
				if (answer.For == ticket && !answer.Locations.empty())
				{
					return answer.Locations.front();
				}
			}

			std::this_thread::sleep_for(std::chrono::seconds{1});
		}

		return std::nullopt;
	}

	void ServeDemoWith(std::string_view serverId)
	{
		TempDirectory temp;
		const std::filesystem::path servers = temp.GetPath() / "servers";
		InstallForReal(serverId, servers);

		temp.MakeFile("project/Demo.slnx", kSolution);
		temp.MakeFile("project/Demo/Demo.csproj", kProject);
		const std::filesystem::path source = temp.MakeFile("project/Demo/Program.cs", kProgram);
		const std::string chosen{serverId};

		Graphite::LanguageServices services{temp.GetPath() / "project"};
		services.SetServersDirectory(servers);
		services.SetLogDirectory(temp.MakeDirectory("logs"));
		services.SetChoiceProvider([chosen](Graphite::LanguageId language) { return Graphite::ChooseServer(language, chosen); });
		services.SetSelectedSolution(temp.GetPath() / "project" / "Demo.slnx");

		services.EnsureStarted(source, Graphite::LanguageId::CSharp);
		services.OpenDocument(source, Graphite::LanguageId::CSharp, kProgram);

		REQUIRE(WaitUntilReady(services, source));

		const std::optional<Graphite::Location> definition = DefinitionOfHello(services, source);
		REQUIRE(definition.has_value());
		CHECK(definition->Line == 1);
		CHECK(definition->Column == Graphite::Utf16Column{44});
	}
}

TEST_CASE("csharp-ls installed by Graphite answers a definition in a solution" * doctest::skip())
{
	ServeDemoWith("csharp-ls");
}

TEST_CASE("Roslyn installed by Graphite loads the solution and answers a definition" * doctest::skip())
{
	ServeDemoWith("roslyn");
}

TEST_CASE("OmniSharp installed by Graphite answers a definition in a solution" * doctest::skip())
{
	ServeDemoWith("omnisharp");
}
