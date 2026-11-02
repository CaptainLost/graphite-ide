#include <doctest/doctest.h>

#include "DeferredJobs.h"
#include "FakeHttpClient.h"
#include "ImGuiFrames.h"
#include "TempAppSettings.h"
#include "TempDirectory.h"

#include "App/SettingsPages.h"
#include "Editor/EditorSettings.h"
#include "Language/Install/ServerInstalls.h"
#include "Language/Server/LanguageServices.h"
#include "Search/SearchSettings.h"
#include "Sessions/ClaudeSettings.h"
#include "Settings/AnalysisSettings.h"
#include "Settings/Panel/SettingsPanel.h"
#include "Settings/Panel/SettingsSearch.h"
#include "Terminal/TerminalWindows.h"
#include "Theme/ActiveScheme.h"
#include "UI/Dialogs/ModalGate.h"
#include "Workspace/Workspace.h"
#include "Workspace/WorkspaceTrust.h"

#include "Core/Command/Binding.h"
#include "Core/Command/Command.h"
#include "Core/Command/CommandRegistry.h"
#include "Core/Command/Keymap.h"
#include "Core/Events/EventBus.h"
#include "Core/ImGui/Animation.h"

#include <imgui.h>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace {

	constexpr float kPanelHeight = 700.0f;
	constexpr float kWidePanelWidth = 1100.0f;
	constexpr float kNarrowPanelWidth = 600.0f;

	struct ApplicationParts
	{
		TempDirectory Temp;
		TempAppSettings Settings;
		Core::EventBus Events;
		Graphite::Workspace Workspace{Events, Settings.Get()};
		Core::Keymap Keys;
		Core::CommandRegistry Commands;
		Graphite::EditorSettings Editor{Settings.Get()};
		Graphite::SearchSettings Search{Settings.Get()};
		Graphite::AnalysisSettings Analysis{Settings.Get()};
		Graphite::ClaudeSettings Claude{Settings.Get()};
		Graphite::ActiveScheme Scheme;
		Graphite::ModalGate Modals;
		Graphite::WorkspaceTrust Trust{Temp.GetPath() / "trust.json"};
		Core::TypingClaim Typing;
		Graphite::TerminalWindows Terminals{Workspace, Keys, Commands, Settings.Get(), Events, Typing};
		DeferredJobs Jobs;
		Graphite::ServerInstalls Installs{Core::CreateRef<FakeHttpClient>(), Temp.GetPath() / "servers", Jobs.Submitter()};
		Graphite::LanguageServices Services{Temp.GetPath()};
		bool SolutionLoaded = false;

		Graphite::SettingsSources Sources()
		{
			return Graphite::SettingsSources{.Store = Settings.Get(),
											 .Editor = Editor,
											 .Search = Search,
											 .Analysis = Analysis,
											 .Claude = Claude,
											 .Scheme = Scheme,
											 .Commands = Commands,
											 .Keys = Keys,
											 .Modals = Modals,
											 .Events = Events,
											 .Trust = Trust,
											 .Terminals = Terminals,
											 .Installs = Installs,
											 .Services = Services,
											 .SolutionLoaded = SolutionLoaded,
											 .ThemesDirectory = Temp.GetPath() / "themes",
											 .OnKeyBindingsChanged = [] {}};
		}
	};

	std::vector<std::string> IdsOf(const Graphite::SettingsGroup& group)
	{
		std::vector<std::string> ids;

		for (const Core::Scope<Graphite::SettingsPage>& page : group.Pages)
		{
			ids.emplace_back(page->GetId());
		}

		return ids;
	}

	std::vector<std::string> EveryRowTitle(const Graphite::SettingsDirectory& directory)
	{
		std::vector<std::string> titles;

		for (Graphite::SettingsPage* page : Graphite::CollectPages(directory))
		{
			for (const Graphite::SettingsCard& card : page->Describe())
			{
				titles.push_back(card.Heading);

				for (const Graphite::SettingsRow& row : card.Rows)
				{
					titles.push_back(row.Title);
				}
			}
		}

		return titles;
	}

	bool Contains(const std::vector<std::string>& titles, const std::string& wanted)
	{
		for (const std::string& title : titles)
		{
			if (title == wanted)
			{
				return true;
			}
		}

		return false;
	}

	void DrawInWindow(Graphite::SettingsPanel& panel, float width)
	{
		Core::Animation::BeginFrame();
		ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
		ImGui::SetNextWindowSize(ImVec2(width, kPanelHeight));
		ImGui::Begin("Settings");
		panel.OnImGuiRender();
		ImGui::End();
	}

	std::string ReadTextOrEmptyWhenMissing(const std::filesystem::path& file)
	{
		std::ifstream stream{file, std::ios::binary};
		std::ostringstream text;
		text << stream.rdbuf();
		return text.str();
	}

	const std::vector<std::vector<std::string>> kEveryPagePath{
		{"appearance"}, {"keyBindings"}, {"languageServers", "Cpp"}, {"languageServers", "CSharp"}, {"languageServers", "backgroundAnalysis"}, {"trustedFolders"}, {"editor"}, {"explorer"}, {"findInFiles"}, {"git"}, {"terminal"}, {"claudeSessions"}};

	void DrawEveryPageAndSearch(Graphite::SettingsPanel& panel, ImGuiFrames& frames)
	{
		for (const std::vector<std::string>& path : kEveryPagePath)
		{
			CAPTURE(path.back());
			REQUIRE(panel.Open(path));
			frames.Next([&panel] { DrawInWindow(panel, kWidePanelWidth); });
			frames.Next([&panel] { DrawInWindow(panel, kNarrowPanelWidth); });
		}

		for (const char* query : {"wrap", "git", "colour", "server"})
		{
			panel.SetQuery(query);
			frames.Next([&panel] { DrawInWindow(panel, kWidePanelWidth); });
			frames.Next([&panel] { DrawInWindow(panel, kNarrowPanelWidth); });
		}
	}
}

TEST_CASE("The sidebar holds General, then Panels named as the View menu names them")
{
	ApplicationParts parts;

	const Graphite::SettingsDirectory directory = Graphite::BuildSettingsDirectory(parts.Sources());

	REQUIRE(directory.GetGroups().size() == 2);
	CHECK(directory.GetGroups()[0].Title == "General");
	CHECK(IdsOf(directory.GetGroups()[0]) == std::vector<std::string>{"appearance", "keyBindings", "languageServers", "trustedFolders"});
	CHECK(directory.GetGroups()[1].Title == "Panels");
	CHECK(IdsOf(directory.GetGroups()[1]) == std::vector<std::string>{"editor", "explorer", "findInFiles", "git", "terminal", "claudeSessions"});
}

TEST_CASE("Every setting the application had before the pages is on some page")
{
	ApplicationParts parts;
	const Graphite::SettingsDirectory directory = Graphite::BuildSettingsDirectory(parts.Sources());
	ImGuiFrames frames;
	std::vector<std::string> titles;

	frames.Next([&titles, &directory] { titles = EveryRowTitle(directory); });

	for (const char* wanted : {"Font size", "Word wrap", "Indent with", "Tab width", "Symbol information", "Definition preview", "Hover delay",
							   "Markers in the gutter", "Markers on the scrollbar", "Wrap long lines", "Files ignored by Git",
							   "Open folders to the active file", "Respect .gitignore", "Only files listed in projects", "Interface scale",
							   "Colour scheme", "This scheme", "Default shell", "Claude program", "Prices", "Shortcuts", "Restore all defaults",
							   "Language server", "Analyse in the background", "Analysis kept", "Set the number of files automatically",
							   "Files analysed at once", "Trusted folders"})
	{
		CAPTURE(wanted);
		CHECK(Contains(titles, wanted));
	}
}

TEST_CASE("Every page draws inside the panel, and the search over all of them, without an ImGui assertion")
{
	ApplicationParts parts;
	Graphite::SettingsPanel panel{Graphite::BuildSettingsDirectory(parts.Sources()), parts.Scheme};
	ImGuiFrames frames;

	DrawEveryPageAndSearch(panel, frames);

	CHECK(panel.IsSearching());
}

TEST_CASE("Drawing and searching every page writes nothing to graphite.json, since nothing is written until a control is used")
{
	ApplicationParts parts;
	parts.Settings.Get().Flush();
	const std::string before = ReadTextOrEmptyWhenMissing(parts.Settings.GetFile());
	Graphite::SettingsPanel panel{Graphite::BuildSettingsDirectory(parts.Sources()), parts.Scheme};
	ImGuiFrames frames;

	DrawEveryPageAndSearch(panel, frames);
	parts.Settings.Get().Flush();

	CHECK(ReadTextOrEmptyWhenMissing(parts.Settings.GetFile()) == before);
}

TEST_CASE("Searching for a command's name or a word of its shortcut lists it under Key Bindings")
{
	ApplicationParts parts;
	parts.Commands.Add(Core::Command{.Id = "file.save", .Label = "Save File"});
	parts.Keys.SetDefault("file.save", Core::Binding{.Primary = Core::Chord::Key(ImGuiMod_Ctrl | ImGuiKey_S)});
	const Graphite::SettingsDirectory directory = Graphite::BuildSettingsDirectory(parts.Sources());
	ImGuiFrames frames;

	for (const char* query : {"save file", "ctrl save"})
	{
		CAPTURE(query);
		std::vector<Graphite::SettingsPageHits> hits;
		frames.Next([&hits, &directory, query] { hits = Graphite::FindSettings(directory, query); });

		const Graphite::SettingsPageHits* keyBindings = nullptr;

		for (const Graphite::SettingsPageHits& page : hits)
		{
			if (page.Label == "Key Bindings")
			{
				keyBindings = &page;
			}
		}

		REQUIRE(keyBindings != nullptr);
		REQUIRE(keyBindings->Cards.size() == 1);
		REQUIRE(keyBindings->Cards[0].Rows.size() == 1);
		CHECK(keyBindings->Cards[0].Rows[0].Title == "Save File");
		CHECK(keyBindings->Cards[0].Rows[0].Description == "Ctrl+S");
		CHECK(keyBindings->Cards[0].Rows[0].OpenWith == "Save File");
	}
}
