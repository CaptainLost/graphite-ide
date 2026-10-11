#include <doctest/doctest.h>

#include "Language/Api/HoverInfo.h"
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

	std::optional<Graphite::LanguageServices::Answer> AwaitAnswer(Graphite::LanguageServices& services, Graphite::LanguageServices::Ticket ticket,
																  std::chrono::steady_clock::time_point deadline)
	{
		while (std::chrono::steady_clock::now() < deadline)
		{
			std::this_thread::sleep_for(std::chrono::milliseconds{100});
			services.Poll();

			std::vector<Graphite::LanguageServices::Answer> answers;
			services.TakeAnswers(answers);

			for (Graphite::LanguageServices::Answer& answer : answers)
			{
				if (answer.For == ticket)
				{
					return std::move(answer);
				}
			}
		}

		return std::nullopt;
	}

	// A server still loading may answer a definition with nothing, so it is asked again until it names something.
	std::optional<Graphite::Location> DefinitionOfHello(Graphite::LanguageServices& services, const std::filesystem::path& source)
	{
		const std::chrono::steady_clock::time_point deadline = std::chrono::steady_clock::now() + kLiveTimeout;
		const Graphite::FilePosition hello{.Path = source, .Line = 2, .Column = Graphite::Utf16Column{77}};

		while (std::chrono::steady_clock::now() < deadline)
		{
			const Graphite::LanguageServices::Ticket ticket = services.AskDefinition(source, Graphite::LanguageId::CSharp, hello);
			const std::optional<Graphite::LanguageServices::Answer> answer = AwaitAnswer(services, ticket, deadline);

			if (answer && !answer->Locations.empty())
			{
				return answer->Locations.front();
			}

			std::this_thread::sleep_for(std::chrono::seconds{1});
		}

		return std::nullopt;
	}

	// A server still loading may answer a hover with nothing, so it is asked again until it says something.
	std::optional<Graphite::HoverInfo> HoverOverWriteLine(Graphite::LanguageServices& services, const std::filesystem::path& source)
	{
		const std::chrono::steady_clock::time_point deadline = std::chrono::steady_clock::now() + kLiveTimeout;
		const Graphite::FilePosition insideWriteLine{.Path = source, .Line = 2, .Column = Graphite::Utf16Column{61}};

		while (std::chrono::steady_clock::now() < deadline)
		{
			const Graphite::LanguageServices::Ticket ticket = services.AskHover(source, Graphite::LanguageId::CSharp, insideWriteLine);
			const std::optional<Graphite::LanguageServices::Answer> answer = AwaitAnswer(services, ticket, deadline);

			if (answer && !answer->Hover.Declaration.empty())
			{
				return answer->Hover;
			}

			std::this_thread::sleep_for(std::chrono::seconds{1});
		}

		return std::nullopt;
	}

	using ServedCheck = std::function<void(Graphite::LanguageServices& services, const std::filesystem::path& source)>;

	void CheckDefinitionOfHello(Graphite::LanguageServices& services, const std::filesystem::path& source)
	{
		const std::optional<Graphite::Location> definition = DefinitionOfHello(services, source);
		REQUIRE(definition.has_value());
		CHECK(definition->Line == 1);
		CHECK(definition->Column == Graphite::Utf16Column{44});
	}

	void CheckHoverOverWriteLine(Graphite::LanguageServices& services, const std::filesystem::path& source)
	{
		const std::optional<Graphite::HoverInfo> hover = HoverOverWriteLine(services, source);
		REQUIRE(hover.has_value());
		MESSAGE(hover->Declaration);
		MESSAGE(hover->OverloadNote);
		MESSAGE(hover->Documentation);

		CHECK(hover->Declaration.find("WriteLine") != std::string::npos);
		CHECK_FALSE(hover->Documentation.empty());
		CHECK(hover->Declaration.find('\r') == std::string::npos);
		CHECK(hover->Declaration.find("&nbsp;") == std::string::npos);
		CHECK(hover->Declaration.find("\\.") == std::string::npos);
		CHECK(hover->Declaration.find("```") == std::string::npos);
		CHECK(hover->Declaration.find("(+") == std::string::npos);
		CHECK(hover->Documentation.find('\r') == std::string::npos);
		CHECK(hover->Documentation.find("&nbsp;") == std::string::npos);
		CHECK(hover->Documentation.find("\\.") == std::string::npos);
		CHECK(hover->Documentation.find("```") == std::string::npos);
	}

	struct DemoFolder
	{
		std::filesystem::path Root;
		std::filesystem::path Source;
		std::optional<std::filesystem::path> SelectedSolution;
	};

	void ServeAndCheck(TempDirectory& temp, const DemoFolder& folder, std::string_view serverId, const ServedCheck& check)
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

		check(services, folder.Source);
	}

	void ServeDemoWith(std::string_view serverId, const ServedCheck& check)
	{
		TempDirectory temp;
		temp.MakeFile("project/Demo.slnx", kSolution);
		temp.MakeFile("project/Demo/Demo.csproj", kProject);

		ServeAndCheck(temp,
					  DemoFolder{.Root = temp.GetPath() / "project",
								 .Source = temp.MakeFile("project/Demo/Program.cs", kProgram),
								 .SelectedSolution = temp.GetPath() / "project" / "Demo.slnx"},
					  serverId, check);
	}
}

TEST_CASE("csharp-ls installed by Graphite answers a definition in a solution" * doctest::skip())
{
	ServeDemoWith("csharp-ls", CheckDefinitionOfHello);
}

TEST_CASE("Roslyn installed by Graphite loads the solution and answers a definition" * doctest::skip())
{
	ServeDemoWith("roslyn", CheckDefinitionOfHello);
}

TEST_CASE("Roslyn installed by Graphite loads a lone project and answers a definition" * doctest::skip())
{
	TempDirectory temp;
	temp.MakeFile("project/Demo.csproj", kProject);

	ServeAndCheck(temp, DemoFolder{.Root = temp.GetPath() / "project", .Source = temp.MakeFile("project/Program.cs", kProgram)}, "roslyn",
				  CheckDefinitionOfHello);
}

TEST_CASE("OmniSharp installed by Graphite answers a definition in a solution" * doctest::skip())
{
	ServeDemoWith("omnisharp", CheckDefinitionOfHello);
}

TEST_CASE("Roslyn installed by Graphite loads projects found below a folder without a solution" * doctest::skip())
{
	TempDirectory temp;
	temp.MakeFile("project/src/Demo/Demo.csproj", kProject);

	ServeAndCheck(temp, DemoFolder{.Root = temp.GetPath() / "project", .Source = temp.MakeFile("project/src/Demo/Program.cs", kProgram)},
				  "roslyn", CheckDefinitionOfHello);
}

TEST_CASE("csharp-ls installed by Graphite answers a hover as a declaration and its documentation" * doctest::skip())
{
	ServeDemoWith("csharp-ls", CheckHoverOverWriteLine);
}

TEST_CASE("Roslyn installed by Graphite answers a hover as a declaration and its documentation" * doctest::skip())
{
	ServeDemoWith("roslyn", CheckHoverOverWriteLine);
}

TEST_CASE("OmniSharp installed by Graphite answers a hover as a declaration and its documentation" * doctest::skip())
{
	ServeDemoWith("omnisharp", CheckHoverOverWriteLine);
}
