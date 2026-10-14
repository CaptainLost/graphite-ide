#include <doctest/doctest.h>

#include "Language/Api/HoverInfo.h"
#include "Language/Server/LanguageServices.h"
#include "Language/Server/ServerCatalog.h"
#include "Language/Server/ServerChoice.h"
#include "LiveInstalls.h"
#include "TempDirectory.h"

#include "Editor/StatusBar/ServerStatusText.h"

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

	constexpr std::chrono::milliseconds kPollInterval{100};
	constexpr std::chrono::seconds kRetryInterval{1};

	constexpr std::size_t kHelloCallLine = 2;
	constexpr std::size_t kHelloCallColumn = 77;
	constexpr std::size_t kHelloDefinitionLine = 1;
	constexpr std::size_t kHelloDefinitionColumn = 44;
	constexpr std::size_t kWriteLineLine = 2;
	constexpr std::size_t kWriteLineColumn = 61;

	constexpr const char* kProgram = "namespace Demo;\n"
									 "static class Greeter { public static string Hello(string name) => $\"Hello {name}\"; }\n"
									 "static class Program { static void Main() { System.Console.WriteLine(Greeter.Hello(\"x\")); } }\n";

	bool HasStoppedForGood(Graphite::ServerState state)
	{
		return state == Graphite::ServerState::Failed || state == Graphite::ServerState::NotInstalled ||
			   state == Graphite::ServerState::MissingRuntime;
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

			if (HasStoppedForGood(status.State))
			{
				MESSAGE(std::format("stopped waiting at {}", status.Detail));
				return false;
			}

			std::this_thread::sleep_for(kPollInterval);
		}

		return false;
	}

	std::optional<Graphite::LanguageServices::Answer> AwaitAnswer(Graphite::LanguageServices& services, Graphite::LanguageServices::Ticket ticket,
																  std::chrono::steady_clock::time_point deadline)
	{
		while (std::chrono::steady_clock::now() < deadline)
		{
			std::this_thread::sleep_for(kPollInterval);
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

	std::optional<Graphite::Location> DefinitionOfHello(Graphite::LanguageServices& services, const std::filesystem::path& source)
	{
		const std::chrono::steady_clock::time_point deadline = std::chrono::steady_clock::now() + kLiveTimeout;
		const Graphite::FilePosition hello{.Path = source, .Line = kHelloCallLine, .Column = Graphite::Utf16Column{kHelloCallColumn}};

		while (std::chrono::steady_clock::now() < deadline)
		{
			const Graphite::LanguageServices::Ticket ticket = services.AskDefinition(source, Graphite::LanguageId::CSharp, hello);
			const std::optional<Graphite::LanguageServices::Answer> answer = AwaitAnswer(services, ticket, deadline);

			if (answer && !answer->Locations.empty())
			{
				return answer->Locations.front();
			}

			std::this_thread::sleep_for(kRetryInterval);
		}

		return std::nullopt;
	}

	std::optional<Graphite::HoverInfo> HoverOverWriteLine(Graphite::LanguageServices& services, const std::filesystem::path& source)
	{
		const std::chrono::steady_clock::time_point deadline = std::chrono::steady_clock::now() + kLiveTimeout;
		const Graphite::FilePosition insideWriteLine{.Path = source, .Line = kWriteLineLine, .Column = Graphite::Utf16Column{kWriteLineColumn}};

		while (std::chrono::steady_clock::now() < deadline)
		{
			const Graphite::LanguageServices::Ticket ticket = services.AskHover(source, Graphite::LanguageId::CSharp, insideWriteLine);
			const std::optional<Graphite::LanguageServices::Answer> answer = AwaitAnswer(services, ticket, deadline);

			if (answer && !answer->Hover.Declaration.empty())
			{
				return answer->Hover;
			}

			std::this_thread::sleep_for(kRetryInterval);
		}

		return std::nullopt;
	}

	using ServedCheck = std::function<void(Graphite::LanguageServices& services, const std::filesystem::path& source)>;

	void CheckDefinitionOfHello(Graphite::LanguageServices& services, const std::filesystem::path& source)
	{
		const std::optional<Graphite::Location> definition = DefinitionOfHello(services, source);
		REQUIRE(definition.has_value());
		CHECK(definition->Line == kHelloDefinitionLine);
		CHECK(definition->Column == Graphite::Utf16Column{kHelloDefinitionColumn});
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
		RequireInstalled(*Graphite::FindServer(serverId), servers);
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
