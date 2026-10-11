#include <doctest/doctest.h>

#include "DeferredJobs.h"
#include "FakeHttpClient.h"
#include "ImGuiFrames.h"
#include "InstalledServers.h"
#include "TempAppSettings.h"
#include "TempDirectory.h"

#include "App/LanguageServerChoice.h"
#include "Language/Install/ServerInstalls.h"
#include "Language/Server/LanguageServices.h"
#include "Language/Server/ServerChoice.h"
#include "Settings/AnalysisSettings.h"
#include "Settings/LanguagesSection.h"
#include "Theme/ActiveScheme.h"

#include <imgui.h>

#include <cstddef>
#include <filesystem>
#include <optional>
#include <ostream>
#include <string>
#include <vector>

namespace {

	// The section inside a window, as the settings panel draws it, over an installer whose jobs wait for RunAll.
	struct LanguagesScreen
	{
		TempDirectory Temp;
		std::filesystem::path Servers = Temp.GetPath() / "servers";
		TempAppSettings Settings;
		Graphite::AnalysisSettings Analysis{Settings.Get()};
		DeferredJobs Jobs;
		Graphite::ServerInstalls Installs{Core::CreateRef<FakeHttpClient>(), Servers, Jobs.Submitter()};
		Graphite::LanguageServices Services{Temp.GetPath()};
		Graphite::ActiveScheme Scheme;
		Graphite::LanguagesSection Section{Analysis, Settings.Get(), Installs, Services, Scheme};
		ImGuiFrames Frames;

		LanguagesScreen()
		{
			Services.SetChoiceProvider([this](Graphite::LanguageId language) { return Graphite::ReadServerChoice(Settings.Get(), language); });
		}

		void ReadInstalls()
		{
			Installs.SweepAtStartup();
			Jobs.RunAll();
			Installs.Poll();
		}

		void Draw()
		{
			ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
			ImGui::SetNextWindowSize(ImVec2(900.0f, 600.0f));
			ImGui::Begin("Settings");
			Section.OnImGuiRender();
			ImGui::End();
		}

		void Frames2()
		{
			Frames.Next([this] { Draw(); });
			Frames.Next([this] { Draw(); });
		}
	};
}

TEST_CASE("Selecting a language shows its tab")
{
	LanguagesScreen screen;

	screen.Frames2();
	CHECK(screen.Section.GetShownLanguage() == Graphite::LanguageId::Cpp);

	screen.Section.SelectLanguage(Graphite::LanguageId::CSharp);
	screen.Frames2();
	CHECK(screen.Section.GetShownLanguage() == Graphite::LanguageId::CSharp);

	screen.Section.SelectLanguage(Graphite::LanguageId::Cpp);
	screen.Frames2();
	CHECK(screen.Section.GetShownLanguage() == Graphite::LanguageId::Cpp);
}

TEST_CASE("A tab draws every install state without an ImGui assertion")
{
	LanguagesScreen screen;
	screen.Section.SelectLanguage(Graphite::LanguageId::CSharp);

	screen.Frames2();

	screen.Section.SelectServer(Graphite::LanguageId::CSharp, "roslyn");
	screen.Frames2();

	screen.ReadInstalls();
	screen.Frames2();

	screen.Section.Run(Graphite::InstallAction::Install, "roslyn");
	screen.Frames2();

	screen.Section.Run(Graphite::InstallAction::Cancel, "roslyn");
	screen.Frames2();

	screen.Jobs.RunAll();
	screen.Installs.Poll();
	screen.Frames2();

	screen.Section.SelectServer(Graphite::LanguageId::CSharp, Graphite::kServerTurnedOff);
	screen.Frames2();

	CHECK(screen.Section.GetShownLanguage() == Graphite::LanguageId::CSharp);
}

TEST_CASE("An installed server's tab draws its version and a failed check without an ImGui assertion")
{
	LanguagesScreen screen;
	InstallCsharpLsIn(screen.Servers);
	screen.ReadInstalls();
	screen.Section.SelectLanguage(Graphite::LanguageId::CSharp);
	screen.Section.SelectServer(Graphite::LanguageId::CSharp, "csharp-ls");

	screen.Frames2();

	screen.Section.Run(Graphite::InstallAction::CheckForUpdates, "csharp-ls");
	screen.Jobs.RunAll();
	screen.Installs.Poll();
	screen.Frames2();

	REQUIRE(screen.Installs.ProgressOf("csharp-ls").has_value());
	CHECK(screen.Installs.ProgressOf("csharp-ls")->Step == Graphite::InstallStep::Failed);
}

TEST_CASE("Choosing a server in the tab stores it and the language follows it at once")
{
	LanguagesScreen screen;

	screen.Section.SelectServer(Graphite::LanguageId::CSharp, "csharp-ls");

	CHECK(screen.Settings.Get().GetLanguageServerChoice("CSharp") == std::optional<std::string>{"csharp-ls"});
	CHECK(screen.Services.ChosenServer(Graphite::LanguageId::CSharp) == Graphite::FindServer("csharp-ls"));

	screen.Section.SelectServer(Graphite::LanguageId::CSharp, Graphite::kServerTurnedOff);

	CHECK(screen.Settings.Get().GetLanguageServerChoice("CSharp") == std::optional<std::string>{"none"});
	CHECK(screen.Services.StatusFor(Graphite::LanguageId::CSharp).State == Graphite::ServerState::Off);
}

TEST_CASE("A path and arguments edited in the tab are stored for that server")
{
	LanguagesScreen screen;

	screen.Section.SetServerPath("roslyn", std::filesystem::path{L"D:\\Roslyn\\roslyn.exe"});
	screen.Section.SetServerArguments("roslyn", R"(--logLevel "Trace")");

	const Graphite::LanguageServerSettings stored = screen.Settings.Get().GetLanguageServerSettings("roslyn");

	CHECK(stored.BinaryPath == std::filesystem::path{L"D:\\Roslyn\\roslyn.exe"});
	CHECK(stored.Arguments == std::vector<std::string>{"--logLevel", "Trace"});

	screen.Section.SetServerPath("roslyn", {});
	CHECK(screen.Settings.Get().GetLanguageServerSettings("roslyn").BinaryPath.empty());
}

TEST_CASE("Uninstalling from the tab starts the uninstall")
{
	LanguagesScreen screen;
	InstallCsharpLsIn(screen.Servers);
	screen.ReadInstalls();

	screen.Section.Run(Graphite::InstallAction::Uninstall, "csharp-ls");

	CHECK(screen.Installs.RunningJobOf("csharp-ls") == Graphite::InstallJobKind::Uninstall);
}

TEST_CASE("Choosing the server already chosen reconfigures nothing")
{
	LanguagesScreen screen;
	std::size_t asked = 0;

	screen.Services.SetChoiceProvider([&screen, &asked](Graphite::LanguageId language) {
		++asked;
		return Graphite::ReadServerChoice(screen.Settings.Get(), language);
	});

	// The choice is asked for again only after a reconfigure cleared it, so asking after each selection shows whether one happened.
	screen.Section.SelectServer(Graphite::LanguageId::CSharp, "csharp-ls");
	screen.Services.ChosenServer(Graphite::LanguageId::CSharp);
	const std::size_t askedAfterChoosing = asked;
	REQUIRE(askedAfterChoosing > 0);

	screen.Section.SelectServer(Graphite::LanguageId::CSharp, "csharp-ls");
	screen.Services.ChosenServer(Graphite::LanguageId::CSharp);
	CHECK(asked == askedAfterChoosing);

	screen.Section.SelectServer(Graphite::LanguageId::CSharp, Graphite::kServerTurnedOff);
	screen.Services.ChosenServer(Graphite::LanguageId::CSharp);
	const std::size_t askedAfterTurningOff = asked;
	CHECK(askedAfterTurningOff > askedAfterChoosing);

	screen.Section.SelectServer(Graphite::LanguageId::CSharp, Graphite::kServerTurnedOff);
	screen.Services.ChosenServer(Graphite::LanguageId::CSharp);
	CHECK(asked == askedAfterTurningOff);
}
