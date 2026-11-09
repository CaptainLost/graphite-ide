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
#include "DrawnGlyphs.h"
#include "ImGuiFrames.h"
#include "TempAppSettings.h"
#include "TempDirectory.h"

#include "Core/Base/Timestep.h"
#include "Core/Events/EventBus.h"
#include "Core/Text/DocumentSet.h"

#include <imgui.h>
#include <imgui_internal.h>

#include <cstddef>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>

namespace {

	using Graphite::SearchScope;
	using Graphite::SearchScopeInputs;

	constexpr ImVec2 kPanelSize{600.0f, 400.0f};
	constexpr ImVec2 kInsideTheQueryField{200.0f, 17.0f};
	constexpr int kFramesPastTheLoaderWait = 20;

	constexpr const char* kProjectsUnreadable = "The solution could not be read, so Projects only has no files to search.";

	constexpr const char* kProject = R"(<Project Sdk="Microsoft.NET.Sdk"></Project>)";

	constexpr const char* kOneProjectSolution = R"(<Solution>
  <Project Path="Tools\Tools.csproj" />
</Solution>
)";

	constexpr const char* kBrokenSolution = "<Solution><Project";

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

	std::size_t VisibleCharacters(std::string_view text)
	{
		std::size_t visible = 0;

		for (const char character : text)
		{
			if (character != ' ')
			{
				++visible;
			}
		}

		return visible;
	}

	std::size_t GlyphsInResults()
	{
		constexpr std::string_view kResultsChildName = "##searchResults";

		for (const ImGuiWindow* window : ImGui::GetCurrentContext()->Windows)
		{
			if (std::string_view{window->Name}.find(kResultsChildName) != std::string_view::npos)
			{
				return DrawnGlyphs(*window->DrawList).size();
			}
		}

		return 0;
	}

	struct SearchScreen
	{
		explicit SearchScreen(DeferredJobs& jobs)
			: Jobs(jobs), Workspace(Events, Settings.Get(), Core::DirectoryWatcher::Start, jobs.Submitter())
		{
			Search.SetProjectsOnly(true);
		}

		void Draw()
		{
			ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
			ImGui::SetNextWindowSize(Size);
			ImGui::Begin("Find in Files", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoSavedSettings);
			Panel.OnImGuiRender();
			ImGui::End();
		}

		void Next()
		{
			Panel.OnUpdate(Core::Timestep{});
			Frames.Next([this] { Draw(); });
		}

		void WaitPastTheLoaderWait()
		{
			for (int frameIndex = 0; frameIndex < kFramesPastTheLoaderWait; ++frameIndex)
			{
				Next();
			}
		}

		void TypeInQuery(const char* text)
		{
			Frames.Click(kInsideTheQueryField, [this] { Draw(); });
			ImGui::GetIO().AddInputCharactersUTF8(text);
			Next();
		}

		void SearchFor(const char* needle)
		{
			TypeInQuery(needle);
			Frames.PressKey(ImGuiKey_Enter, [this] { Draw(); });
		}

		void ReadTheSolution(DeferredJobs& jobs)
		{
			jobs.RunAll();
			Workspace.Update();
			Next();
		}

		void WaitUntilTheSearchIsDone()
		{
			Jobs.RunAll();
			Next();

			REQUIRE_FALSE(Panel.IsSearching());
		}

		DeferredJobs& Jobs;
		ImVec2 Size = kPanelSize;
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
		Graphite::SearchService Service{Jobs.Submitter()};
		Graphite::SearchPanel Panel{Service, Search, Workspace, Documents, Scheme, Editor, Semantics};
	};

	void MakeProjectWithNeedles(TempDirectory& folder, const char* solutionText)
	{
		folder.MakeFile("Tools/Tools.csproj", kProject);
		folder.MakeFile("Tools/Inside.cs", "needle\n");
		folder.MakeFile("Elsewhere/Outside.cs", "needle\n");
		folder.MakeFile("App.slnx", solutionText);
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

TEST_CASE("A narrow panel wraps the search options onto further lines instead of cutting off the Search button")
{
	DeferredJobs jobs;
	SearchScreen screen{jobs};
	constexpr float kNarrowWidth = 200.0f;
	screen.Size = ImVec2(kNarrowWidth, kPanelSize.y);

	screen.Next();
	screen.Next();

	const ImGuiWindow* window = ImGui::FindWindowByName("Find in Files");
	REQUIRE(window != nullptr);
	CHECK(window->DC.CursorMaxPos.x <= window->WorkRect.Max.x + 0.5f);
}

TEST_CASE("A Projects only search asked for while the solution is read shows the loader, then searches its projects alone")
{
	TempDirectory folder;
	MakeProjectWithNeedles(folder, kOneProjectSolution);

	DeferredJobs jobs;
	SearchScreen screen{jobs};
	REQUIRE(screen.Workspace.OpenFolder(folder.GetPath()));
	REQUIRE(screen.Workspace.IsReadingSolution());
	screen.Next();

	screen.SearchFor("needle");
	screen.WaitPastTheLoaderWait();

	CHECK_FALSE(screen.Panel.HasSearched());
	CHECK(GlyphsInResults() > 0);

	screen.ReadTheSolution(jobs);
	REQUIRE(screen.Panel.HasSearched());
	screen.WaitUntilTheSearchIsDone();

	CHECK(screen.Panel.GetHitCount() == 1);
}

TEST_CASE("A Projects only search whose solution then fails to read searches nothing and says why")
{
	TempDirectory folder;
	MakeProjectWithNeedles(folder, kBrokenSolution);

	DeferredJobs jobs;
	SearchScreen screen{jobs};
	REQUIRE(screen.Workspace.OpenFolder(folder.GetPath()));
	screen.Next();

	screen.SearchFor("needle");
	screen.ReadTheSolution(jobs);
	screen.Next();

	CHECK_FALSE(screen.Panel.HasSearched());
	CHECK(GlyphsInResults() == VisibleCharacters(kProjectsUnreadable));
}

TEST_CASE("A Projects only search asked for once the solution is known unreadable says why at once and searches nothing")
{
	TempDirectory folder;
	MakeProjectWithNeedles(folder, kBrokenSolution);

	DeferredJobs jobs;
	SearchScreen screen{jobs};
	REQUIRE(screen.Workspace.OpenFolder(folder.GetPath()));
	screen.ReadTheSolution(jobs);
	REQUIRE_FALSE(screen.Workspace.IsReadingSolution());

	screen.SearchFor("needle");
	screen.Next();

	CHECK_FALSE(screen.Panel.HasSearched());
	CHECK(GlyphsInResults() == VisibleCharacters(kProjectsUnreadable));
}

TEST_CASE("The unreadable solution's message gives way to the idle results once Projects only is unticked")
{
	TempDirectory folder;
	MakeProjectWithNeedles(folder, kBrokenSolution);

	DeferredJobs jobs;
	SearchScreen screen{jobs};
	REQUIRE(screen.Workspace.OpenFolder(folder.GetPath()));
	screen.ReadTheSolution(jobs);
	screen.SearchFor("needle");
	screen.Next();
	REQUIRE(GlyphsInResults() == VisibleCharacters(kProjectsUnreadable));

	screen.Search.SetProjectsOnly(false);
	screen.Next();
	screen.Next();

	CHECK(GlyphsInResults() == 0);
	CHECK_FALSE(screen.Panel.HasSearched());
}

TEST_CASE("The unreadable solution's message gives way to the idle results once a later read of the solution succeeds")
{
	TempDirectory folder;
	MakeProjectWithNeedles(folder, kBrokenSolution);

	DeferredJobs jobs;
	SearchScreen screen{jobs};
	REQUIRE(screen.Workspace.OpenFolder(folder.GetPath()));
	screen.ReadTheSolution(jobs);
	screen.SearchFor("needle");
	screen.Next();
	REQUIRE(GlyphsInResults() == VisibleCharacters(kProjectsUnreadable));

	std::ofstream(folder.GetPath() / "App.slnx", std::ios::binary | std::ios::trunc) << kOneProjectSolution;
	screen.Workspace.ReloadSolution();
	screen.ReadTheSolution(jobs);
	REQUIRE(screen.Workspace.GetLoadedSolution().has_value());
	screen.Next();

	CHECK(GlyphsInResults() == 0);
	CHECK_FALSE(screen.Panel.HasSearched());
}

TEST_CASE("A search held for the solution is dropped without a word when the folder closes")
{
	TempDirectory folder;
	MakeProjectWithNeedles(folder, kOneProjectSolution);

	DeferredJobs jobs;
	SearchScreen screen{jobs};
	REQUIRE(screen.Workspace.OpenFolder(folder.GetPath()));
	screen.Next();
	screen.SearchFor("needle");

	screen.Workspace.Close();
	screen.ReadTheSolution(jobs);
	screen.WaitPastTheLoaderWait();

	CHECK_FALSE(screen.Panel.HasSearched());
	CHECK(GlyphsInResults() == 0);
}

TEST_CASE("A search held for the solution is dropped without a word when another folder opens")
{
	TempDirectory folder;
	MakeProjectWithNeedles(folder, kOneProjectSolution);
	TempDirectory otherFolder;
	MakeProjectWithNeedles(otherFolder, kOneProjectSolution);

	DeferredJobs jobs;
	SearchScreen screen{jobs};
	REQUIRE(screen.Workspace.OpenFolder(folder.GetPath()));
	screen.Next();
	screen.SearchFor("needle");

	REQUIRE(screen.Workspace.OpenFolder(otherFolder.GetPath()));
	screen.ReadTheSolution(jobs);
	REQUIRE(screen.Workspace.GetLoadedSolution().has_value());
	screen.WaitPastTheLoaderWait();

	CHECK_FALSE(screen.Panel.HasSearched());
	CHECK(screen.Panel.GetHitCount() == 0);
	CHECK(GlyphsInResults() == 0);
}

TEST_CASE("Editing the query while a search is held does not change the search that runs")
{
	TempDirectory folder;
	MakeProjectWithNeedles(folder, kOneProjectSolution);

	DeferredJobs jobs;
	SearchScreen screen{jobs};
	REQUIRE(screen.Workspace.OpenFolder(folder.GetPath()));
	screen.Next();
	screen.SearchFor("needle");

	screen.TypeInQuery("zzz");

	screen.ReadTheSolution(jobs);
	REQUIRE(screen.Panel.HasSearched());
	screen.WaitUntilTheSearchIsDone();

	CHECK(screen.Panel.GetHitCount() == 1);
}
