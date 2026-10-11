#include <doctest/doctest.h>

#include "DeferredJobs.h"
#include "FakeHttpClient.h"
#include "ImGuiFrames.h"
#include "TempAppSettings.h"
#include "TempDirectory.h"

#include "Language/Install/ServerInstalls.h"
#include "Language/Server/LanguageServices.h"
#include "Settings/AnalysisSettings.h"
#include "Settings/LanguageServers/LanguageServersPage.h"
#include "Settings/Panel/SettingsPanel.h"
#include "Theme/ActiveScheme.h"

#include "Core/ImGui/Animation.h"

#include <imgui.h>

#include <string>
#include <utility>
#include <vector>

namespace {

	struct ServersScreen
	{
		TempDirectory Temp;
		TempAppSettings Settings;
		Graphite::AnalysisSettings Analysis{Settings.Get()};
		DeferredJobs Jobs;
		Graphite::ServerInstalls Installs{Core::CreateRef<FakeHttpClient>(), Temp.GetPath() / "servers", Jobs.Submitter()};
		Graphite::LanguageServices Services{Temp.GetPath()};
		Graphite::ActiveScheme Scheme;
		Graphite::SettingsPanel Panel{Build(), Scheme};
		ImGuiFrames Frames;

		Graphite::SettingsDirectory Build()
		{
			Graphite::SettingsGroup general{.Title = "General"};
			general.Pages.push_back(Core::CreateScope<Graphite::LanguageServersPage>(Analysis, Settings.Get(), Installs, Services));

			std::vector<Graphite::SettingsGroup> groups;
			groups.push_back(std::move(general));

			return Graphite::SettingsDirectory{std::move(groups)};
		}

		void Draw()
		{
			Core::Animation::BeginFrame();
			ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
			ImGui::SetNextWindowSize(ImVec2(1100.0f, 700.0f));
			ImGui::Begin("Settings");
			Panel.OnImGuiRender();
			ImGui::End();
		}
	};

	std::vector<std::string> IdsOf(const std::vector<Graphite::SettingsPage*>& pages)
	{
		std::vector<std::string> ids;

		for (const Graphite::SettingsPage* page : pages)
		{
			ids.emplace_back(page->GetId());
		}

		return ids;
	}
}

TEST_CASE("Language Servers holds a page per language with servers, then Background Analysis")
{
	ServersScreen screen;
	Graphite::LanguageServersPage page{screen.Analysis, screen.Settings.Get(), screen.Installs, screen.Services};

	REQUIRE(page.GetChildren() != nullptr);
	CHECK(IdsOf(Graphite::CollectPages(*page.GetChildren())) == std::vector<std::string>{"Cpp", "CSharp", "backgroundAnalysis"});
	CHECK(page.GetChildren()->GetGroups().size() == 2);
}

TEST_CASE("A language's settings open under Language Servers, the first language when none is named")
{
	ServersScreen screen;

	CHECK(screen.Panel.GetOpenPath() == std::vector<std::string>{"languageServers", "Cpp"});
	REQUIRE(screen.Panel.Open({"languageServers", "CSharp"}));
	CHECK(screen.Panel.GetOpenPath() == std::vector<std::string>{"languageServers", "CSharp"});
}

TEST_CASE("Language Servers draws its inner list and each page in it without an ImGui assertion")
{
	ServersScreen screen;

	for (const char* child : {"Cpp", "CSharp", "backgroundAnalysis"})
	{
		REQUIRE(screen.Panel.Open({"languageServers", child}));
		screen.Frames.Next([&screen] { screen.Draw(); });
		screen.Frames.Next([&screen] { screen.Draw(); });
	}

	screen.Panel.SetQuery("server");
	screen.Frames.Next([&screen] { screen.Draw(); });

	CHECK(screen.Panel.IsSearching());
}
