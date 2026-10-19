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
#include "Language/Server/LspClient.h"
#include "Language/Server/ServerChoice.h"
#include "Language/Server/ServerCommand.h"
#include "Settings/AppSettings.h"
#include "Settings/LanguageServers/LanguageServerPage.h"
#include "Settings/Panel/SettingsLayout.h"
#include "UI/FileIcons.h"

#include "Core/ImGui/Animation.h"

#include <imgui.h>

#include <cstddef>
#include <filesystem>
#include <optional>
#include <ostream>
#include <string>
#include <string_view>
#include <vector>

namespace {

	constexpr float kControlWidth = 300.0f;
	constexpr float kNarrowPageWidth = 320.0f;

	// WARNING: A group's size would not do: an empty group still reaches out to the item before it, the window's title bar.
	bool DrawControlAlone(const Graphite::SettingsRow& row)
	{
		Core::Animation::BeginFrame();
		ImGui::Begin("Control alone");
		ImGui::PushItemWidth(kControlWidth);
		const float cursorBefore = ImGui::GetCursorPosY();
		row.DrawControl();
		const float cursorAfter = ImGui::GetCursorPosY();
		ImGui::PopItemWidth();
		ImGui::End();

		return cursorAfter > cursorBefore;
	}

	struct LanguageServerScreen
	{
		TempDirectory Temp;
		std::filesystem::path Servers = Temp.GetPath() / "servers";
		TempAppSettings Settings;
		DeferredJobs Jobs;
		Graphite::ServerInstalls Installs{Core::CreateRef<FakeHttpClient>(), Servers, Jobs.Submitter()};
		Graphite::LanguageServices Services{Temp.GetPath()};
		Graphite::LanguageId Language;
		std::size_t ChoiceAsks = 0;
		Graphite::LanguageServerPage Page;
		SettingsPageFrames Frames{Page};

		explicit LanguageServerScreen(Graphite::LanguageId language = Graphite::LanguageId::CSharp)
			: Language{language}, Page{language, Settings.Get(), Installs, Services}
		{
			Services.SetChoiceProvider([this](Graphite::LanguageId language) { return AskChoice(language); });
		}

		Graphite::ServerChoice AskChoice(Graphite::LanguageId language)
		{
			++ChoiceAsks;
			return Graphite::ReadServerChoice(Settings.Get(), language);
		}

		// WARNING: The services ask for the choice again only after a reconfigure cleared it, so the count from one settled
		// choice to the next is the number of reconfigures between them.
		std::size_t SettledChoiceAsks()
		{
			Services.ChosenServer(Language);
			return ChoiceAsks;
		}

		bool ControlDrawsSomething(const Graphite::SettingsRow& row)
		{
			bool drewSomething = false;
			Frames.GetFrames().Next([&row, &drewSomething] { drewSomething = DrawControlAlone(row); });
			return drewSomething;
		}

		std::optional<std::string> StoredChoice() { return Settings.Get().GetLanguageServerChoice(Page.GetId()); }

		Graphite::LanguageServerSettings Stored(std::string_view serverId) { return Settings.Get().GetLanguageServerSettings(serverId); }

		void StoreCustomServer(std::string_view serverId, const std::filesystem::path& path)
		{
			Settings.Get().SetLanguageServerChoice(Page.GetId(), serverId);
			Settings.Get().SetLanguageServerBinaryPath(serverId, path);
		}

		std::vector<std::string> RowTitlesIn(std::string_view heading)
		{
			std::vector<std::string> titles;

			for (const Graphite::SettingsCard& card : Frames.DescribeInFrame())
			{
				if (card.Heading != heading)
				{
					continue;
				}

				for (const Graphite::SettingsRow& row : card.Rows)
				{
					titles.push_back(row.Title);
				}
			}

			return titles;
		}

		float ButtonsWidth(const std::vector<std::string>& labels)
		{
			float width = 0.0f;
			Frames.GetFrames().Next([&width, &labels] { width = Graphite::SettingsLayout::ButtonsWidth(labels); });
			return width;
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

	Graphite::ServerChoice ClangdRunningFrom(Graphite::LanguageId language, const std::filesystem::path& binary)
	{
		Graphite::ServerChoice choice = Graphite::ChooseServer(language, std::string{"clangd"});
		choice.Overrides.Binary = binary;
		return choice;
	}

	Core::Scope<Graphite::LspClient> MakeStandInClient(const Graphite::ServerLaunch& launch, const std::filesystem::path& root)
	{
		return Core::CreateScope<Graphite::LspClient>(launch, root, Graphite::LspClient::WithoutServer{});
	}

	const std::filesystem::path kRoslynCopy{L"D:\\Tools\\Roslyn\\roslyn.exe"};
	const std::filesystem::path kClangdCopy{L"D:\\Tools\\clangd\\clangd.exe"};
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

TEST_CASE("A language's page shows the language's file badge in place of a glyph")
{
	LanguageServerScreen cpp{Graphite::LanguageId::Cpp};
	LanguageServerScreen csharp{Graphite::LanguageId::CSharp};

	CHECK(cpp.Page.GetBadge() == std::optional<Graphite::FileKind>{Graphite::FileKind::CppSource});
	CHECK(csharp.Page.GetBadge() == std::optional<Graphite::FileKind>{Graphite::FileKind::CSharp});
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
	screen.Frames.DrawPageTwice(kNarrowPageWidth);

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

	// WARNING: The choice is asked for again only after a reconfigure cleared it, so asking after each selection shows whether one happened.
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

TEST_CASE("The word custom finds a language's page")
{
	LanguageServerScreen screen;

	const std::optional<Graphite::SettingsRow> row = screen.Frames.FindRow("Language server");

	REQUIRE(row.has_value());
	CHECK(HasKeyword(*row, "custom"));
}

TEST_CASE("A server chosen from the list shows Install and an Advanced card with Arguments alone")
{
	LanguageServerScreen screen;

	screen.Page.SelectServer("roslyn");

	CHECK(screen.Frames.CardHeadings() == std::vector<std::string>{"Server", "Install", "Advanced"});
	CHECK(screen.RowTitlesIn("Advanced") == std::vector<std::string>{"Arguments"});
}

TEST_CASE("Choosing Custom shows the custom server card and stores nothing until a path is set")
{
	LanguageServerScreen screen;

	screen.Page.SelectCustom();
	screen.Frames.DrawPageTwice();

	CHECK(screen.Frames.CardHeadings() == std::vector<std::string>{"Server", "Custom server"});
	CHECK(screen.RowTitlesIn("Custom server") == std::vector<std::string>{"Server type", "Path", "Arguments"});
	CHECK_FALSE(screen.StoredChoice().has_value());
	CHECK(screen.Stored("roslyn").BinaryPath.empty());

	screen.Settings.Get().Flush();
	CHECK_FALSE(std::filesystem::exists(screen.Settings.GetFile()));
}

TEST_CASE("Choosing Custom while a server is chosen keeps that choice stored as it was")
{
	LanguageServerScreen screen;
	screen.Page.SelectServer("csharp-ls");

	screen.Page.SelectCustom();

	CHECK(screen.Frames.CardHeadings() == std::vector<std::string>{"Server", "Custom server"});
	CHECK(screen.StoredChoice() == std::optional<std::string>{"csharp-ls"});
	CHECK(screen.Stored("csharp-ls").BinaryPath.empty());
}

TEST_CASE("A stored path makes the page read as Custom")
{
	LanguageServerScreen screen;

	screen.StoreCustomServer("roslyn", kRoslynCopy);

	CHECK(screen.Frames.CardHeadings() == std::vector<std::string>{"Server", "Custom server"});

	const std::optional<Graphite::SettingsRow> pathRow = screen.Frames.FindRow("Path");

	REQUIRE(pathRow.has_value());
	CHECK(pathRow->Description == "Starts when a C# file is opened.");
}

TEST_CASE("Opening a page that reads as Custom writes nothing to the settings")
{
	LanguageServerScreen screen{Graphite::LanguageId::Cpp};
	screen.StoreCustomServer("clangd", kClangdCopy);
	screen.Settings.Get().Flush();
	std::filesystem::remove(screen.Settings.GetFile());

	screen.Frames.DrawPageTwice();
	screen.Frames.DrawPageTwice(kNarrowPageWidth);
	screen.Page.OnDeactivated();

	CHECK(screen.Frames.CardHeadings() == std::vector<std::string>{"Server", "Custom server"});

	screen.Settings.Get().Flush();
	CHECK_FALSE(std::filesystem::exists(screen.Settings.GetFile()));
}

TEST_CASE("Setting the path under Custom chooses the server type and stores the path")
{
	LanguageServerScreen screen;

	screen.Page.SelectCustom();
	screen.Page.SetCustomPath(kRoslynCopy);

	CHECK(screen.StoredChoice() == std::optional<std::string>{"roslyn"});
	CHECK(screen.Stored("roslyn").BinaryPath == kRoslynCopy);
	CHECK(screen.Services.ChosenServer(Graphite::LanguageId::CSharp) == Graphite::FindServer("roslyn"));
	CHECK(screen.Frames.CardHeadings() == std::vector<std::string>{"Server", "Custom server"});
}

TEST_CASE("Setting the path under Custom keeps the server already chosen as the server type")
{
	LanguageServerScreen screen;
	screen.Page.SelectServer("omnisharp");

	screen.Page.SelectCustom();
	screen.Page.SetCustomPath(kRoslynCopy);

	CHECK(screen.StoredChoice() == std::optional<std::string>{"omnisharp"});
	CHECK(screen.Stored("omnisharp").BinaryPath == kRoslynCopy);
	CHECK(screen.Stored("roslyn").BinaryPath.empty());
}

TEST_CASE("Clearing the path under Custom removes it and keeps the custom server card")
{
	LanguageServerScreen screen;
	screen.StoreCustomServer("roslyn", kRoslynCopy);

	const std::filesystem::path noPath;
	screen.Page.SetCustomPath(noPath);

	CHECK(screen.Stored("roslyn").BinaryPath.empty());
	CHECK(screen.StoredChoice() == std::optional<std::string>{"roslyn"});
	CHECK(screen.Frames.CardHeadings() == std::vector<std::string>{"Server", "Custom server"});
}

TEST_CASE("Changing the server type under Custom moves the path and arguments to the new server")
{
	LanguageServerScreen screen;
	screen.StoreCustomServer("roslyn", kRoslynCopy);
	screen.Settings.Get().SetLanguageServerArguments("roslyn", {"--logLevel", "Trace"});

	screen.Page.SetCustomServerType("csharp-ls");

	CHECK(screen.StoredChoice() == std::optional<std::string>{"csharp-ls"});
	CHECK(screen.Stored("csharp-ls").BinaryPath == kRoslynCopy);
	CHECK(screen.Stored("csharp-ls").Arguments == std::vector<std::string>{"--logLevel", "Trace"});
	CHECK(screen.Stored("roslyn").BinaryPath.empty());
	CHECK(screen.Stored("roslyn").Arguments.empty());
	CHECK(screen.Services.ChosenServer(Graphite::LanguageId::CSharp) == Graphite::FindServer("csharp-ls"));
	CHECK(screen.Frames.CardHeadings() == std::vector<std::string>{"Server", "Custom server"});
}

TEST_CASE("Changing the server type before a path is set changes only the page")
{
	LanguageServerScreen screen;
	screen.Page.SelectCustom();

	screen.Page.SetCustomServerType("csharp-ls");

	CHECK_FALSE(screen.StoredChoice().has_value());
	CHECK(screen.Frames.CardHeadings() == std::vector<std::string>{"Server", "Custom server"});

	screen.Page.SetCustomPath(kRoslynCopy);

	CHECK(screen.StoredChoice() == std::optional<std::string>{"csharp-ls"});
	CHECK(screen.Stored("csharp-ls").BinaryPath == kRoslynCopy);
}

TEST_CASE("Choosing a server from the list leaves Custom and clears the custom path")
{
	LanguageServerScreen screen;
	screen.StoreCustomServer("roslyn", kRoslynCopy);

	screen.Page.SelectServer("csharp-ls");

	CHECK(screen.StoredChoice() == std::optional<std::string>{"csharp-ls"});
	CHECK(screen.Stored("roslyn").BinaryPath.empty());
	CHECK(screen.Frames.CardHeadings() == std::vector<std::string>{"Server", "Install", "Advanced"});
}

TEST_CASE("Choosing the custom server's own entry in the list runs it from Graphite's install again")
{
	LanguageServerScreen screen{Graphite::LanguageId::Cpp};
	screen.StoreCustomServer("clangd", kClangdCopy);

	screen.Page.SelectServer("clangd");

	CHECK(screen.StoredChoice() == std::optional<std::string>{"clangd"});
	CHECK(screen.Stored("clangd").BinaryPath.empty());
	CHECK(screen.Frames.CardHeadings() == std::vector<std::string>{"Server", "Install", "Advanced"});
}

TEST_CASE("Choosing a listed server whose path was left from before does not read as Custom")
{
	LanguageServerScreen screen;
	screen.Settings.Get().SetLanguageServerChoice("CSharp", "roslyn");
	screen.Settings.Get().SetLanguageServerBinaryPath("csharp-ls", kRoslynCopy);

	screen.Page.SelectServer("csharp-ls");

	CHECK(screen.Stored("csharp-ls").BinaryPath.empty());
	CHECK(screen.Frames.CardHeadings() == std::vector<std::string>{"Server", "Install", "Advanced"});
}

TEST_CASE("Choosing a server from the list leaves a Custom with no path yet")
{
	LanguageServerScreen screen;
	screen.Page.SelectCustom();

	screen.Page.SelectServer("roslyn");

	CHECK(screen.StoredChoice() == std::optional<std::string>{"roslyn"});
	CHECK(screen.Frames.CardHeadings() == std::vector<std::string>{"Server", "Install", "Advanced"});
}

TEST_CASE("Choosing None from Custom clears the custom path and turns the language off")
{
	LanguageServerScreen screen;
	screen.StoreCustomServer("roslyn", kRoslynCopy);

	screen.Page.SelectServer(Graphite::kServerTurnedOff);

	CHECK(screen.StoredChoice() == std::optional<std::string>{"none"});
	CHECK(screen.Stored("roslyn").BinaryPath.empty());
	CHECK(screen.Services.StatusFor(Graphite::LanguageId::CSharp).State == Graphite::ServerState::Off);
	CHECK(screen.Frames.CardHeadings() == std::vector<std::string>{"Server"});
}

TEST_CASE("The C++ page under Custom has no server type row")
{
	LanguageServerScreen screen{Graphite::LanguageId::Cpp};

	screen.Page.SelectCustom();

	CHECK(screen.Frames.CardHeadings() == std::vector<std::string>{"Server", "Custom server"});
	CHECK(screen.RowTitlesIn("Custom server") == std::vector<std::string>{"Path", "Arguments"});

	screen.Page.SetCustomPath(kClangdCopy);

	CHECK(screen.StoredChoice() == std::optional<std::string>{"clangd"});
	CHECK(screen.RowTitlesIn("Custom server") == std::vector<std::string>{"Path", "Arguments"});
}

TEST_CASE("The custom server card draws with and without a path without an ImGui assertion")
{
	LanguageServerScreen screen;
	screen.ReadInstalls();

	screen.Page.SelectCustom();
	screen.Frames.DrawPageTwice();
	screen.Frames.DrawPageTwice(kNarrowPageWidth);

	screen.Page.SetCustomPath(kRoslynCopy);
	screen.Frames.DrawPageTwice();
	screen.Frames.DrawPageTwice(kNarrowPageWidth);

	screen.Page.SetCustomServerType("omnisharp");
	screen.Frames.DrawPageTwice();
	screen.Frames.DrawPageTwice(kNarrowPageWidth);

	const std::filesystem::path noPath;
	screen.Page.SetCustomPath(noPath);
	screen.Frames.DrawPageTwice();
	screen.Frames.DrawPageTwice(kNarrowPageWidth);

	screen.Page.SelectServer("roslyn");
	screen.Frames.DrawPageTwice();

	CHECK(screen.Frames.CardHeadings() == std::vector<std::string>{"Server", "Install", "Advanced"});

	LanguageServerScreen cppScreen{Graphite::LanguageId::Cpp};
	cppScreen.StoreCustomServer("clangd", kClangdCopy);
	cppScreen.Frames.DrawPageTwice();
	cppScreen.Frames.DrawPageTwice(kNarrowPageWidth);

	CHECK(cppScreen.Frames.CardHeadings() == std::vector<std::string>{"Server", "Custom server"});
}

TEST_CASE("A custom path that does not exist shows as the path's problem")
{
	LanguageServerScreen screen{Graphite::LanguageId::Cpp};
	const std::filesystem::path missingCopy = screen.Temp.GetPath() / "missing" / "clangd.exe";
	screen.Page.SelectCustom();
	screen.Page.SetCustomPath(missingCopy);

	screen.Services.EnsureStarted(screen.Temp.GetPath() / "main.cpp", Graphite::LanguageId::Cpp);
	const std::optional<Graphite::SettingsRow> row = screen.Frames.FindRow("Path");

	REQUIRE(row.has_value());
	CHECK(row->Description.empty());
	CHECK(row->Problem.find("does not exist") != std::string::npos);
}

TEST_CASE("A server running from outside Graphite's install reads Found on PATH and offers to install Graphite's copy")
{
	LanguageServerScreen screen{Graphite::LanguageId::Cpp};
	const std::filesystem::path outsideCopy = screen.Temp.MakeFile("tools/clangd.exe");
	screen.Services.SetClientFactory(MakeStandInClient);
	screen.Page.SelectServer("clangd");
	screen.ReadInstalls();

	// WARNING: Handed to the services alone, so the settings the page reads store no path, as for a copy found on PATH.
	screen.Services.SetChoiceProvider([outsideCopy](Graphite::LanguageId language) { return ClangdRunningFrom(language, outsideCopy); });
	screen.Services.EnsureStarted(screen.Temp.GetPath() / "main.cpp", Graphite::LanguageId::Cpp);

	const std::optional<Graphite::ServerCommand> found = screen.Services.FoundCommandFor(Graphite::LanguageId::Cpp);
	const std::optional<Graphite::SettingsRow> row = screen.InstallRow();

	REQUIRE(found.has_value());
	REQUIRE(row.has_value());
	CHECK(row->Title == "Found on PATH");
	CHECK(row->Description == "In use: " + Graphite::DescribeCommand(*found) + ". Installing puts Graphite's copy ahead of it.");
	CHECK(row->ControlWidth == screen.ButtonsWidth({"Install Graphite's copy"}));

	screen.Frames.DrawPageTwice();
	screen.Frames.DrawPageTwice(kNarrowPageWidth);
}

TEST_CASE("A server neither installed nor running still reads Not installed")
{
	LanguageServerScreen screen{Graphite::LanguageId::Cpp};
	screen.Page.SelectServer("clangd");
	screen.ReadInstalls();

	const std::optional<Graphite::SettingsRow> row = screen.InstallRow();

	REQUIRE(row.has_value());
	CHECK(row->Title == "Not installed");
	CHECK(row->ControlWidth == screen.ButtonsWidth({"Install"}));
}

TEST_CASE("A page with a server chosen from the list is still found by path and binary")
{
	LanguageServerScreen screen;
	screen.Page.SelectServer("roslyn");

	const std::optional<Graphite::SettingsRow> row = screen.Frames.FindRow("Language server");

	REQUIRE(row.has_value());
	CHECK(HasKeyword(*row, "path"));
	CHECK(HasKeyword(*row, "binary"));
}

TEST_CASE("Setting the path under Custom from nothing chosen reconfigures the language once")
{
	LanguageServerScreen screen;
	screen.Page.SelectCustom();
	const std::size_t askedBefore = screen.SettledChoiceAsks();

	screen.Page.SetCustomPath(kRoslynCopy);

	CHECK(screen.SettledChoiceAsks() == askedBefore + 1);
}

TEST_CASE("Changing the server type with a path in use reconfigures the language once")
{
	LanguageServerScreen screen;
	screen.StoreCustomServer("roslyn", kRoslynCopy);
	const std::size_t askedBefore = screen.SettledChoiceAsks();

	screen.Page.SetCustomServerType("csharp-ls");

	CHECK(screen.SettledChoiceAsks() == askedBefore + 1);
}

TEST_CASE("Choosing a listed server from a stored Custom reconfigures the language once")
{
	LanguageServerScreen screen;
	screen.StoreCustomServer("roslyn", kRoslynCopy);
	const std::size_t askedBeforeOtherServer = screen.SettledChoiceAsks();

	screen.Page.SelectServer("csharp-ls");

	CHECK(screen.SettledChoiceAsks() == askedBeforeOtherServer + 1);

	LanguageServerScreen cppScreen{Graphite::LanguageId::Cpp};
	cppScreen.StoreCustomServer("clangd", kClangdCopy);
	const std::size_t askedBeforeSameServer = cppScreen.SettledChoiceAsks();

	cppScreen.Page.SelectServer("clangd");

	CHECK(cppScreen.SettledChoiceAsks() == askedBeforeSameServer + 1);
}

TEST_CASE("A row described for a server the page no longer shows draws nothing")
{
	LanguageServerScreen screen;
	screen.StoreCustomServer("roslyn", kRoslynCopy);
	const std::optional<Graphite::SettingsRow> pathBeforeTypeChange = screen.Frames.FindRow("Path");
	const std::optional<Graphite::SettingsRow> argumentsBeforeTypeChange = screen.Frames.FindRow("Arguments");
	REQUIRE(pathBeforeTypeChange.has_value());
	REQUIRE(argumentsBeforeTypeChange.has_value());

	screen.Page.SetCustomServerType("csharp-ls");

	const std::optional<Graphite::SettingsRow> pathAfterTypeChange = screen.Frames.FindRow("Path");
	REQUIRE(pathAfterTypeChange.has_value());

	CHECK_FALSE(screen.ControlDrawsSomething(*pathBeforeTypeChange));
	CHECK_FALSE(screen.ControlDrawsSomething(*argumentsBeforeTypeChange));
	CHECK(screen.ControlDrawsSomething(*pathAfterTypeChange));

	screen.Page.SelectServer("omnisharp");
	const std::optional<Graphite::SettingsRow> advancedArguments = screen.Frames.FindRow("Arguments");
	REQUIRE(advancedArguments.has_value());

	screen.Page.SelectServer("roslyn");
	const std::optional<Graphite::SettingsRow> currentArguments = screen.Frames.FindRow("Arguments");
	REQUIRE(currentArguments.has_value());

	CHECK_FALSE(screen.ControlDrawsSomething(*advancedArguments));
	CHECK(screen.ControlDrawsSomething(*currentArguments));
}
