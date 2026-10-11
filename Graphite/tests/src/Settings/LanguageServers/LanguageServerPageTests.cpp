#include <doctest/doctest.h>

#include "DeferredJobs.h"
#include "FakeHttpClient.h"
#include "InstalledServers.h"
#include "SettingsPageFrames.h"
#include "TempAppSettings.h"
#include "TempDirectory.h"

#include "App/LanguageServerChoice.h"
#include "Language/Install/ServerInstalls.h"
#include "Language/Server/LanguageServices.h"
#include "Language/Server/ServerChoice.h"
#include "Settings/LanguageServers/LanguageServerPage.h"

#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace {

	// The C# page over an installer whose jobs wait for RunAll.
	struct LanguageServerScreen
	{
		TempDirectory Temp;
		std::filesystem::path Servers = Temp.GetPath() / "servers";
		TempAppSettings Settings;
		DeferredJobs Jobs;
		Graphite::ServerInstalls Installs{Core::CreateRef<FakeHttpClient>(), Servers, Jobs.Submitter()};
		Graphite::LanguageServices Services{Temp.GetPath()};
		Graphite::LanguageServerPage Page{Graphite::LanguageId::CSharp, Settings.Get(), Installs, Services};
		SettingsPageFrames Frames{Page};

		LanguageServerScreen()
		{
			Services.SetChoiceProvider([this](Graphite::LanguageId language) { return Graphite::ReadServerChoice(Settings.Get(), language); });
		}

		void ReadInstalls()
		{
			Installs.SweepAtStartup();
			Jobs.RunAll();
			Installs.Poll();
		}

		std::optional<Graphite::SettingsRow> InstallRow()
		{
			for (const Graphite::SettingsCard& card : Frames.DescribeInFrame())
			{
				if (card.Heading == "Install" && !card.Rows.empty())
				{
					return card.Rows.front();
				}
			}

			return std::nullopt;
		}
	};

	bool HasKeyword(const Graphite::SettingsRow& row, const std::string& keyword)
	{
		for (const std::string& candidate : row.Keywords)
		{
			if (candidate == keyword)
			{
				return true;
			}
		}

		return false;
	}
}

TEST_CASE("A language's page is named after the language and shows the install only once a server is chosen")
{
	LanguageServerScreen screen;

	CHECK(screen.Page.GetId() == "CSharp");
	CHECK(screen.Page.GetTitle() == "C#");
	CHECK(screen.Frames.CardHeadings() == std::vector<std::string>{"Server"});

	screen.Page.SelectServer("roslyn");

	CHECK(screen.Frames.CardHeadings() == std::vector<std::string>{"Server", "Install", "Advanced"});
}

TEST_CASE("A language's page draws every install state without an ImGui assertion")
{
	LanguageServerScreen screen;

	screen.Frames.DrawPageTwice();

	screen.Page.SelectServer("roslyn");
	screen.Frames.DrawPageTwice();

	screen.ReadInstalls();
	screen.Frames.DrawPageTwice();

	screen.Page.Run(Graphite::InstallAction::Install, "roslyn");
	screen.Frames.DrawPageTwice();
	screen.Frames.DrawPageTwice(320.0f);

	screen.Page.Run(Graphite::InstallAction::Cancel, "roslyn");
	screen.Frames.DrawPageTwice();

	screen.Jobs.RunAll();
	screen.Installs.Poll();
	screen.Frames.DrawPageTwice();

	screen.Page.SelectServer(Graphite::kServerTurnedOff);
	screen.Frames.DrawPageTwice();

	CHECK(screen.Frames.CardHeadings() == std::vector<std::string>{"Server"});
}

TEST_CASE("An installed server's page draws its version and a failed check without an ImGui assertion")
{
	LanguageServerScreen screen;
	InstallCsharpLsIn(screen.Servers);
	screen.ReadInstalls();
	screen.Page.SelectServer("csharp-ls");

	screen.Frames.DrawPageTwice();

	screen.Page.Run(Graphite::InstallAction::CheckForUpdates, "csharp-ls");
	screen.Jobs.RunAll();
	screen.Installs.Poll();
	screen.Frames.DrawPageTwice();

	REQUIRE(screen.Installs.ProgressOf("csharp-ls").has_value());
	CHECK(screen.Installs.ProgressOf("csharp-ls")->Step == Graphite::InstallStep::Failed);
}

TEST_CASE("Choosing a server on the page stores it and the language follows it at once")
{
	LanguageServerScreen screen;

	screen.Page.SelectServer("csharp-ls");

	CHECK(screen.Settings.Get().GetLanguageServerChoice("CSharp") == std::optional<std::string>{"csharp-ls"});
	CHECK(screen.Services.ChosenServer(Graphite::LanguageId::CSharp) == Graphite::FindServer("csharp-ls"));

	screen.Page.SelectServer(Graphite::kServerTurnedOff);

	CHECK(screen.Settings.Get().GetLanguageServerChoice("CSharp") == std::optional<std::string>{"none"});
	CHECK(screen.Services.StatusFor(Graphite::LanguageId::CSharp).State == Graphite::ServerState::Off);
}

TEST_CASE("A path and arguments edited on the page are stored for that server")
{
	LanguageServerScreen screen;

	screen.Page.SetServerPath("roslyn", std::filesystem::path{L"D:\\Roslyn\\roslyn.exe"});
	screen.Page.SetServerArguments("roslyn", R"(--logLevel "Trace")");

	const Graphite::LanguageServerSettings stored = screen.Settings.Get().GetLanguageServerSettings("roslyn");

	CHECK(stored.BinaryPath == std::filesystem::path{L"D:\\Roslyn\\roslyn.exe"});
	CHECK(stored.Arguments == std::vector<std::string>{"--logLevel", "Trace"});

	screen.Page.SetServerPath("roslyn", {});
	CHECK(screen.Settings.Get().GetLanguageServerSettings("roslyn").BinaryPath.empty());
}

TEST_CASE("Uninstalling from the page starts the uninstall")
{
	LanguageServerScreen screen;
	InstallCsharpLsIn(screen.Servers);
	screen.ReadInstalls();

	screen.Page.Run(Graphite::InstallAction::Uninstall, "csharp-ls");

	CHECK(screen.Installs.RunningJobOf("csharp-ls") == Graphite::InstallJobKind::Uninstall);
}

TEST_CASE("Choosing the server already chosen reconfigures nothing")
{
	LanguageServerScreen screen;
	std::size_t asked = 0;

	screen.Services.SetChoiceProvider([&screen, &asked](Graphite::LanguageId language) {
		++asked;
		return Graphite::ReadServerChoice(screen.Settings.Get(), language);
	});

	// The choice is asked for again only after a reconfigure cleared it, so asking after each selection shows whether one happened.
	screen.Page.SelectServer("csharp-ls");
	screen.Services.ChosenServer(Graphite::LanguageId::CSharp);
	const std::size_t askedAfterChoosing = asked;
	REQUIRE(askedAfterChoosing > 0);

	screen.Page.SelectServer("csharp-ls");
	screen.Services.ChosenServer(Graphite::LanguageId::CSharp);
	CHECK(asked == askedAfterChoosing);

	screen.Page.SelectServer(Graphite::kServerTurnedOff);
	screen.Services.ChosenServer(Graphite::LanguageId::CSharp);
	const std::size_t askedAfterTurningOff = asked;
	CHECK(askedAfterTurningOff > askedAfterChoosing);

	screen.Page.SelectServer(Graphite::kServerTurnedOff);
	screen.Services.ChosenServer(Graphite::LanguageId::CSharp);
	CHECK(asked == askedAfterTurningOff);
}

TEST_CASE("The install row keeps its key while its state changes")
{
	LanguageServerScreen screen;
	screen.Page.SelectServer("roslyn");
	screen.ReadInstalls();

	const std::optional<Graphite::SettingsRow> beforeInstall = screen.InstallRow();
	screen.Page.Run(Graphite::InstallAction::Install, "roslyn");
	const std::optional<Graphite::SettingsRow> duringInstall = screen.InstallRow();

	REQUIRE(beforeInstall.has_value());
	REQUIRE(duringInstall.has_value());
	CHECK(beforeInstall->GetKey() == "install");
	CHECK(duringInstall->GetKey() == "install");
	CHECK_FALSE(duringInstall->Title.empty());
}

TEST_CASE("A server's name finds its language's page")
{
	LanguageServerScreen screen;

	const std::optional<Graphite::SettingsRow> row = screen.Frames.FindRow("Language server");

	REQUIRE(row.has_value());
	CHECK(HasKeyword(*row, "roslyn"));
	CHECK(HasKeyword(*row, "Roslyn"));
}
