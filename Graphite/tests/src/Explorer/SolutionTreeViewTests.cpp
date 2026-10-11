#include <doctest/doctest.h>

#include "Explorer/SolutionTreeView.h"
#include "Workspace/Workspace.h"

#include "ImGuiFrames.h"
#include "TempAppSettings.h"
#include "TempDirectory.h"

#include "Core/Events/EventBus.h"
#include "Core/Text/DocumentSet.h"

#include <imgui.h>
#include <imgui_internal.h>

#include <cmath>
#include <filesystem>
#include <string_view>
#include <vector>

namespace {

	constexpr ImVec2 kExplorerSize{400.0f, 600.0f};

	// Over the filter box, which fills the view's first line.
	constexpr ImVec2 kFilterBox{200.0f, 17.0f};

	constexpr float kTreeIndentForEachLevel = 21.0f;
	constexpr float kIntoTheLabel = 40.0f;

	constexpr const char* kSolution = R"(<Solution>
  <Folder Name="/Group/">
    <Project Path="Engine\Engine.csproj" />
  </Folder>
  <Project Path="Tools\Tools.csproj" />
</Solution>
)";

	constexpr const char* kProject = R"(<Project Sdk="Microsoft.NET.Sdk"></Project>)";

	const ImGuiWindow* FindSolutionBody()
	{
		constexpr std::string_view kSolutionBodyName = "##solutionBody";

		for (const ImGuiWindow* window : ImGui::GetCurrentContext()->Windows)
		{
			if (std::string_view{window->Name}.find(kSolutionBodyName) != std::string_view::npos)
			{
				return window;
			}
		}

		return nullptr;
	}

	float RowPitch()
	{
		return ImGui::GetFontSize() + ImGui::GetStyle().ItemSpacing.y;
	}

	class SolutionScreen
	{
	public:
		explicit SolutionScreen(const std::filesystem::path& solution)
		{
			REQUIRE(Workspace.OpenSolution(solution));

			Events.Subscribe<Graphite::OpenFileRequested>(
				[this](const Graphite::OpenFileRequested& request) { OpenedFiles.push_back(request.Path); });

			Next();
			Next();
		}

		void Draw()
		{
			ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
			ImGui::SetNextWindowSize(kExplorerSize);
			ImGui::Begin("Explorer", nullptr,
						 ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
							 ImGuiWindowFlags_NoSavedSettings);
			View.Render();
			ImGui::End();
		}

		void Next()
		{
			Frames.Next([this] { Draw(); });
		}

		void Click(ImVec2 at)
		{
			Frames.Click(at, [this] { Draw(); });
		}

		void TypeInFilter(const char* text)
		{
			Click(kFilterBox);
			ImGui::GetIO().AddInputCharactersUTF8(text);
			Next();
			Next();
		}

		void ClearFilterWithBackspaces(int characterCount)
		{
			for (int characterIndex = 0; characterIndex < characterCount; ++characterIndex)
			{
				Frames.PressKey(ImGuiKey_Backspace, [this] { Draw(); });
			}

			Next();
		}

		// Rows are counted from the top of the tree, the solution's own row being 0.
		void ClickRow(int rowIndex, int depth)
		{
			const ImGuiWindow* body = FindSolutionBody();
			REQUIRE(body != nullptr);

			const float x = body->DC.CursorStartPos.x + static_cast<float>(depth) * kTreeIndentForEachLevel + kIntoTheLabel;
			const float y = body->DC.CursorStartPos.y + static_cast<float>(rowIndex) * RowPitch() + ImGui::GetFontSize() * 0.5f;

			Click(ImVec2(x, y));
		}

		int CountDrawnRows()
		{
			const ImGuiWindow* body = FindSolutionBody();
			REQUIRE(body != nullptr);

			return static_cast<int>(std::lround((body->ContentSize.y + ImGui::GetStyle().ItemSpacing.y) / RowPitch()));
		}

		ImGuiFrames Frames;
		TempAppSettings Settings;
		Core::EventBus Events;
		Graphite::Workspace Workspace{Events, Settings.Get()};
		Core::DocumentSet Documents;
		Graphite::SolutionTreeView View{Workspace, Documents, Events};
		std::vector<std::filesystem::path> OpenedFiles;
	};

	std::filesystem::path MakeSampleSolution(TempDirectory& folder)
	{
		folder.MakeFile("Engine/Engine.csproj", kProject);
		folder.MakeFile("Engine/Other.cs");
		folder.MakeFile("Engine/Render/Deep.cs");
		folder.MakeFile("Tools/Tools.csproj", kProject);
		folder.MakeFile("Tools/Tool.cs");
		return folder.MakeFile("App.slnx", kSolution);
	}
}

TEST_CASE("The solution view's filter finds a file in a project nobody opened, and a click on it opens the file")
{
	TempDirectory folder;
	const std::filesystem::path solution = MakeSampleSolution(folder);

	SolutionScreen screen{solution};
	screen.TypeInFilter("Deep");

	const int deepRow = 4;
	const int deepDepth = 4;
	screen.ClickRow(deepRow, deepDepth);

	REQUIRE(screen.OpenedFiles.size() == 1);
	CHECK(std::filesystem::equivalent(screen.OpenedFiles[0], folder.GetPath() / "Engine" / "Render" / "Deep.cs"));
}

TEST_CASE("The solution view's filter leaves out the projects and folders that hold no match")
{
	TempDirectory folder;
	const std::filesystem::path solution = MakeSampleSolution(folder);

	SolutionScreen screen{solution};
	screen.TypeInFilter("Deep");

	const int solutionGroupProjectFolderAndFile = 5;
	CHECK(screen.CountDrawnRows() == solutionGroupProjectFolderAndFile);
}

TEST_CASE("Clearing the solution view's filter leaves the tree as it was before")
{
	TempDirectory folder;
	const std::filesystem::path solution = MakeSampleSolution(folder);

	SolutionScreen screen{solution};
	const int rowsBeforeFiltering = screen.CountDrawnRows();

	screen.TypeInFilter("Deep");
	screen.ClearFilterWithBackspaces(4);

	CHECK(screen.CountDrawnRows() == rowsBeforeFiltering);
}
