#include <doctest/doctest.h>

#include "Language/Install/InstalledServer.h"
#include "Language/Install/ServerInstaller.h"
#include "Language/Server/LanguageServices.h"
#include "Language/Server/ServerCatalog.h"
#include "Language/Server/ServerChoice.h"
#include "TempDirectory.h"

#include "Editor/StatusBar/ServerStatusText.h"

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
		const std::chrono::steady_clock::time_point started = std::chrono::steady_clock::now();
		const std::chrono::steady_clock::time_point deadline = started + kLiveTimeout;
		std::string lastSaid;

		while (std::chrono::steady_clock::now() < deadline)
		{
			services.Poll();

			const Graphite::ServerStatus status = services.StatusFor(source, Graphite::LanguageId::CSharp);
			const std::string said = Graphite::ServerStatusText(status);

			if (said != lastSaid)
			{
				const std::chrono::milliseconds elapsed =
					std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started);
				MESSAGE(std::format("{} ms: {}", elapsed.count(), said));
				lastSaid = said;
			}

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

	struct DemoFolder
	{
		std::filesystem::path Root;
		std::filesystem::path Source;
		std::optional<std::filesystem::path> SelectedSolution;
	};

	void AnswerDefinitionIn(TempDirectory& temp, const DemoFolder& folder, std::string_view serverId)
	{
		const std::filesystem::path servers = temp.GetPath() / "servers";
		InstallForReal(serverId, servers);
		const std::string chosen{serverId};

		Graphite::LanguageServices services{folder.Root};
		services.SetServersDirectory(servers);
		services.SetLogDirectory(temp.MakeDirectory("logs"));
		services.SetChoiceProvider([chosen](Graphite::LanguageId language) { return Graphite::ChooseServer(language, chosen); });

		if (folder.SelectedSolution)
		{
			services.SetSelectedSolution(*folder.SelectedSolution);
		}

		services.EnsureStarted(folder.Source, Graphite::LanguageId::CSharp);
		services.OpenDocument(folder.Source, Graphite::LanguageId::CSharp, kProgram);

		REQUIRE(WaitUntilReady(services, folder.Source));

		const std::optional<Graphite::Location> definition = DefinitionOfHello(services, folder.Source);
		REQUIRE(definition.has_value());
		CHECK(definition->Line == 1);
		CHECK(definition->Column == Graphite::Utf16Column{44});
	}

	void ServeDemoWith(std::string_view serverId)
	{
		TempDirectory temp;
		temp.MakeFile("project/Demo.slnx", kSolution);
		temp.MakeFile("project/Demo/Demo.csproj", kProject);

		AnswerDefinitionIn(temp,
						   DemoFolder{.Root = temp.GetPath() / "project",
									  .Source = temp.MakeFile("project/Demo/Program.cs", kProgram),
									  .SelectedSolution = temp.GetPath() / "project" / "Demo.slnx"},
						   serverId);
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

TEST_CASE("Roslyn installed by Graphite loads a lone project and answers a definition" * doctest::skip())
{
	TempDirectory temp;
	temp.MakeFile("project/Demo.csproj", kProject);

	AnswerDefinitionIn(temp, DemoFolder{.Root = temp.GetPath() / "project", .Source = temp.MakeFile("project/Program.cs", kProgram)}, "roslyn");
}

TEST_CASE("OmniSharp installed by Graphite answers a definition in a solution" * doctest::skip())
{
	ServeDemoWith("omnisharp");
}
