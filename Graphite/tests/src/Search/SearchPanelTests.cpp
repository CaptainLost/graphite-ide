#include <doctest/doctest.h>

#include "Editor/EditorSettings.h"
#include "Editor/Server/PreviewSemantics.h"
#include "Search/SearchPanel.h"
#include "Search/SearchScope.h"
#include "Search/SearchService.h"
#include "Search/SearchSettings.h"
#include "Theme/ActiveScheme.h"
#include "Workspace/Workspace.h"

#include "DeferredJobs.h"
#include "ImGuiFrames.h"
#include "TempAppSettings.h"
#include "TempDirectory.h"

#include "Core/Base/Timestep.h"
#include "Core/Events/EventBus.h"
#include "Core/Text/DocumentSet.h"

#include <imgui.h>

#include <chrono>
#include <cstddef>
#include <filesystem>
#include <string>
#include <thread>

namespace {

	using Graphite::SearchScope;
	using Graphite::SearchScopeInputs;

	constexpr ImVec2 kPanelSize{600.0f, 400.0f};
	constexpr ImVec2 kInsideTheQueryField{200.0f, 17.0f};
	constexpr std::chrono::milliseconds kLongerThanASearchTakes{300};

	constexpr const char* kProject = R"(<Project Sdk="Microsoft.NET.Sdk"></Project>)";

	constexpr const char* kOneProjectSolution = R"(<Solution>
  <Project Path="Tools\Tools.csproj" />
</Solution>
)";

	class SilentPreviewPort final : public Graphite::PreviewServerPort
	{
	public:
		void Open(const std::filesystem::path&, Graphite::LanguageId, std::string) override {}

		Graphite::LanguageServices::Ticket Ask(const std::filesystem::path&, Graphite::LanguageId) override
		{
			return Graphite::LanguageServices::Ticket{};
		}

		void Close(const std::filesystem::path&, Graphite::LanguageId) override {}
	};

	struct SearchScreen
	{
		explicit SearchScreen(DeferredJobs& jobs)
			: Workspace(Events, Settings.Get(), Core::DirectoryWatcher::Start, jobs.Submitter())
		{
			Search.SetProjectsOnly(true);
		}

		void Draw()
		{
			ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
			ImGui::SetNextWindowSize(kPanelSize);
			ImGui::Begin("Find in Files", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoSavedSettings);
			Panel.OnImGuiRender();
			ImGui::End();
		}

		void Next()
		{
			Panel.OnUpdate(Core::Timestep{});
			Frames.Next([this] { Draw(); });
		}

		void SearchFor(const char* needle)
		{
			Frames.Click(kInsideTheQueryField, [this] { Draw(); });
			ImGui::GetIO().AddInputCharactersUTF8(needle);
			Next();
			Frames.PressKey(ImGuiKey_Enter, [this] { Draw(); });
		}

		std::size_t HitsAfterAWhile()
		{
			const std::chrono::steady_clock::time_point until = std::chrono::steady_clock::now() + kLongerThanASearchTakes;

			while (std::chrono::steady_clock::now() < until)
			{
				Next();
			}

			return Panel.GetHitCount();
		}

		ImGuiFrames Frames;
		TempAppSettings Settings;
		Core::EventBus Events;
		Graphite::Workspace Workspace;
		Graphite::SearchSettings Search{Settings.Get()};
		Graphite::EditorSettings Editor{Settings.Get()};
		Graphite::ActiveScheme Scheme;
		Core::DocumentSet Documents;
		SilentPreviewPort Port;
		Graphite::PreviewSemantics Semantics{Port};
		Graphite::SearchService Service;
		Graphite::SearchPanel Panel{Service, Search, Workspace, Documents, Scheme, Editor, Semantics};
	};

	void MakeProjectWithNeedles(TempDirectory& folder)
	{
		folder.MakeFile("Tools/Tools.csproj", kProject);
		folder.MakeFile("Tools/Inside.cs", "needle\n");
		folder.MakeFile("Elsewhere/Outside.cs", "needle\n");
	}
}

TEST_CASE("Projects only searches the projects of a solution read, and the whole folder only when none is chosen")
{
	CHECK(Graphite::ScopeOfSearch(SearchScopeInputs{.ProjectsOnly = false, .SolutionSelected = true, .SolutionLoaded = true}) ==
		  SearchScope::WholeFolder);
	CHECK(Graphite::ScopeOfSearch(SearchScopeInputs{.ProjectsOnly = true, .SolutionSelected = false}) ==
		  SearchScope::WholeFolder);
	CHECK(Graphite::ScopeOfSearch(SearchScopeInputs{.ProjectsOnly = true, .SolutionSelected = true, .SolutionLoaded = true}) ==
		  SearchScope::Projects);
	CHECK(Graphite::ScopeOfSearch(SearchScopeInputs{.ProjectsOnly = true,
													.SolutionSelected = true,
													.SolutionLoaded = true,
													.SolutionBeingRead = true}) == SearchScope::Projects);
	CHECK(Graphite::ScopeOfSearch(SearchScopeInputs{.ProjectsOnly = true, .SolutionSelected = true, .SolutionBeingRead = true}) ==
		  SearchScope::WaitForSolution);
	CHECK(Graphite::ScopeOfSearch(SearchScopeInputs{.ProjectsOnly = true, .SolutionSelected = true}) ==
		  SearchScope::SolutionUnreadable);
}

TEST_CASE("A Projects only search asked for while the solution is read waits for it, then searches its projects alone")
{
	TempDirectory folder;
	MakeProjectWithNeedles(folder);
	folder.MakeFile("App.slnx", kOneProjectSolution);

	DeferredJobs jobs;
	SearchScreen screen{jobs};
	REQUIRE(screen.Workspace.OpenFolder(folder.GetPath()));
	REQUIRE(screen.Workspace.IsReadingSolution());
	screen.Next();

	screen.SearchFor("needle");

	CHECK(screen.HitsAfterAWhile() == 0);

	jobs.RunAll();
	screen.Workspace.Update();

	CHECK(screen.HitsAfterAWhile() == 1);
}

TEST_CASE("A Projects only search whose solution cannot be read searches nothing")
{
	TempDirectory folder;
	MakeProjectWithNeedles(folder);
	folder.MakeFile("Broken.slnx", "<Solution><Project");

	DeferredJobs jobs;
	SearchScreen screen{jobs};
	REQUIRE(screen.Workspace.OpenFolder(folder.GetPath()));
	screen.Next();

	screen.SearchFor("needle");
	jobs.RunAll();
	screen.Workspace.Update();

	CHECK(screen.HitsAfterAWhile() == 0);
	CHECK_FALSE(screen.Workspace.GetLoadedSolution().has_value());
}
