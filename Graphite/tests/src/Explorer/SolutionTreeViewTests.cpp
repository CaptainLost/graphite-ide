#include <doctest/doctest.h>

#include "Explorer/SolutionTreeView.h"
#include "UI/FileIcons.h"
#include "UI/Lists/ListRow.h"
#include "Workspace/Workspace.h"

#include "DrawnColours.h"
#include "ImGuiFrames.h"
#include "TempAppSettings.h"
#include "TempDirectory.h"

#include "Core/Events/EventBus.h"
#include "Core/Text/DocumentSet.h"

#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string_view>
#include <vector>

namespace {

	constexpr ImVec2 kExplorerSize{400.0f, 600.0f};

	constexpr ImVec2 kInsideTheFilterBox{200.0f, 17.0f};

	constexpr float kTreeIndentForEachLevel = 21.0f;
	constexpr float kIntoTheLabel = 40.0f;
	constexpr float kHalf = 0.5f;

	constexpr const char* kSolution = R"(<Solution>
  <Folder Name="/Group/">
    <Project Path="Engine\Engine.csproj" />
  </Folder>
  <Project Path="Tools\Tools.csproj" />
</Solution>
)";

	constexpr const char* kProject = R"(<Project Sdk="Microsoft.NET.Sdk"></Project>)";

	constexpr const char* kOneProjectSolution = R"(<Solution>
  <Project Path="Tools\Tools.csproj" />
</Solution>
)";

	constexpr int kOnlyProjectRow = 1;
	constexpr int kOnlyProjectDepth = 1;

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
		return Graphite::ListRowPitch();
	}

	struct VerticalSpan
	{
		float Top = FLT_MAX;
		float Bottom = -FLT_MAX;
	};

	VerticalSpan VerticalSpanInColour(const ImDrawList& drawing, ImU32 colour)
	{
		VerticalSpan span;

		for (const ImDrawVert& vertex : drawing.VtxBuffer)
		{
			if (vertex.col == colour)
			{
				span.Top = std::min(span.Top, vertex.pos.y);
				span.Bottom = std::max(span.Bottom, vertex.pos.y);
			}
		}

		return span;
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
			Click(kInsideTheFilterBox);
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

		void MoveMouseAway()
		{
			ImGui::GetIO().AddMousePosEvent(-FLT_MAX, -FLT_MAX);
			Next();
		}

		static ImVec2 RowPointCountedFromSolutionRowZero(int rowIndex, int depth)
		{
			const ImGuiWindow* body = FindSolutionBody();
			REQUIRE(body != nullptr);

			const float x = body->DC.CursorStartPos.x + static_cast<float>(depth) * kTreeIndentForEachLevel + kIntoTheLabel;
			const float y = body->DC.CursorStartPos.y + static_cast<float>(rowIndex) * RowPitch() + ImGui::GetFontSize() * kHalf;

			return ImVec2(x, y);
		}

		void ClickRowCountedFromSolutionRowZero(int rowIndex, int depth)
		{
			Click(RowPointCountedFromSolutionRowZero(rowIndex, depth));
		}

		void AskToRemoveTheOnlyProject()
		{
			Frames.Click(RowPointCountedFromSolutionRowZero(kOnlyProjectRow, kOnlyProjectDepth), [this] { Draw(); },
						 ImGuiMouseButton_Right);
			REQUIRE(Frames.ClickMenuItem("Remove from Solution", [this] { Draw(); }));
			Next();
		}

		int CountDrawnRows()
		{
			const ImGuiWindow* body = FindSolutionBody();
			REQUIRE(body != nullptr);

			return static_cast<int>(std::lround(body->ContentSize.y / RowPitch()));
		}

		ImGuiFrames Frames;
		TempAppSettings Settings;
		Core::EventBus Events;
		Graphite::Workspace Workspace{Events, Settings.Get()};
		Core::DocumentSet Documents;
		Graphite::ModalGate Modals;
		Graphite::SolutionTreeView View{Workspace, Documents, Events, Modals};
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

TEST_CASE("The solution view draws the solution and its projects in the project colour and a folder dim")
{
	constexpr bool kDarkScheme = false;

	TempDirectory folder;
	const std::filesystem::path solution = MakeSampleSolution(folder);

	SolutionScreen screen{solution};

	const ImGuiWindow* body = FindSolutionBody();
	REQUIRE(body != nullptr);

	const ImU32 projectColour = Graphite::ColourOf(Graphite::FileKind::Solution, kDarkScheme);
	const VerticalSpan projectRows = VerticalSpanInColour(*body->DrawList, projectColour);

	CHECK(projectRows.Bottom - projectRows.Top > RowPitch());
	CHECK_FALSE(DrawsInColour(*body->DrawList, Graphite::ColourOf(Graphite::FileKind::CSharp, kDarkScheme)));
	CHECK(DrawsInColour(*body->DrawList, Graphite::DimFolderColour()));
}

TEST_CASE("The solution view draws a file's badge in its kind's colour")
{
	constexpr bool kDarkScheme = false;

	TempDirectory folder;
	const std::filesystem::path solution = MakeSampleSolution(folder);
	folder.MakeFile("Tools/settings.json", "{}");

	SolutionScreen screen{solution};
	screen.TypeInFilter("settings");

	const ImGuiWindow* body = FindSolutionBody();
	REQUIRE(body != nullptr);
	CHECK(DrawsInColour(*body->DrawList, Graphite::ColourOf(Graphite::FileKind::Json, kDarkScheme)));
}

TEST_CASE("The solution view's filter finds a file in a project nobody opened, and a click on it opens the file")
{
	TempDirectory folder;
	const std::filesystem::path solution = MakeSampleSolution(folder);

	SolutionScreen screen{solution};
	screen.TypeInFilter("Deep");

	const int deepRow = 4;
	const int deepDepth = 4;
	screen.ClickRowCountedFromSolutionRowZero(deepRow, deepDepth);

	REQUIRE(screen.OpenedFiles.size() == 1);
	CHECK(std::filesystem::equivalent(screen.OpenedFiles[0], folder.GetPath() / "Engine" / "Render" / "Deep.cs"));
}

TEST_CASE("The solution view's filter field spans the panel's width with nothing after it")
{
	constexpr float kFringe = 1.0f;

	TempDirectory folder;
	const std::filesystem::path solution = MakeSampleSolution(folder);

	SolutionScreen screen{solution};

	const ImGuiWindow* explorer = ImGui::FindWindowByName("Explorer");
	REQUIRE(explorer != nullptr);

	const std::optional<HorizontalExtent> field =
		HorizontalExtentInColour(*explorer->DrawList, ImGui::GetColorU32(ImGuiCol_FrameBg));
	REQUIRE(field.has_value());
	CHECK(std::abs(field->Left - (explorer->Pos.x + explorer->WindowPadding.x)) <= kFringe);
	CHECK(std::abs(field->Right - (explorer->Pos.x + explorer->Size.x - explorer->WindowPadding.x)) <= kFringe);
}

TEST_CASE("The solution view marks the row last clicked only while its file's tab is the one in front in the editor")
{
	TempDirectory folder;
	const std::filesystem::path solution = MakeSampleSolution(folder);

	SolutionScreen screen{solution};
	screen.TypeInFilter("Deep");

	const int deepRow = 4;
	const int deepDepth = 4;
	screen.ClickRowCountedFromSolutionRowZero(deepRow, deepDepth);

	REQUIRE(screen.OpenedFiles.size() == 1);
	const std::filesystem::path clicked = screen.OpenedFiles[0];
	REQUIRE(screen.Documents.Open(clicked, Core::TabKind::Permanent) == Core::OpenOutcome::Opened);
	screen.MoveMouseAway();

	const std::filesystem::path noFileInFront;
	screen.View.SetShownFile(noFileInFront);
	screen.Next();

	const ImGuiWindow* body = FindSolutionBody();
	REQUIRE(body != nullptr);
	CHECK_FALSE(MarksARow(*body->DrawList));

	screen.View.SetShownFile(clicked);
	screen.Next();

	body = FindSolutionBody();
	REQUIRE(body != nullptr);
	CHECK(MarksARow(*body->DrawList));
}

TEST_CASE("The solution view marks a file in the preview tab with a ring and one in a permanent tab with a dot")
{
	TempDirectory folder;
	const std::filesystem::path solution = MakeSampleSolution(folder);

	SolutionScreen screen{solution};
	screen.TypeInFilter("Deep");

	const int deepRow = 4;
	const int deepDepth = 4;
	screen.ClickRowCountedFromSolutionRowZero(deepRow, deepDepth);

	REQUIRE(screen.OpenedFiles.size() == 1);
	const std::filesystem::path deep = screen.OpenedFiles[0];

	ImGui::GetStyle().Colors[ImGuiCol_CheckMark] = ImVec4(0.9f, 0.4f, 0.1f, 1.0f);
	const ImU32 checkMarkColourNothingElseShares = ImGui::GetColorU32(ImGuiCol_CheckMark);

	screen.MoveMouseAway();
	const ImGuiWindow* body = FindSolutionBody();
	REQUIRE(body != nullptr);
	CHECK_FALSE(DrawsInColour(*body->DrawList, checkMarkColourNothingElseShares));

	REQUIRE(screen.Documents.Open(deep, Core::TabKind::Preview) == Core::OpenOutcome::Opened);
	screen.Next();
	body = FindSolutionBody();
	REQUIRE(body != nullptr);
	CHECK(DrawsInColour(*body->DrawList, checkMarkColourNothingElseShares));
	CHECK_FALSE(FillsMiddleInColour(*body->DrawList, checkMarkColourNothingElseShares));

	const std::optional<std::size_t> previewIndex = screen.Documents.Find(deep);
	REQUIRE(previewIndex.has_value());
	screen.Documents.Promote(*previewIndex);
	screen.Next();
	body = FindSolutionBody();
	REQUIRE(body != nullptr);
	CHECK(FillsMiddleInColour(*body->DrawList, checkMarkColourNothingElseShares));
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

TEST_CASE("Remove from Solution chosen while another question holds the modal gate waits for it, and asks once the gate is free")
{
	TempDirectory folder;
	folder.MakeFile("Tools/Tools.csproj", kProject);
	const std::filesystem::path solution = folder.MakeFile("App.slnx", kOneProjectSolution);

	SolutionScreen screen{solution};
	constexpr std::string_view kOtherQuestion = "Another question";
	REQUIRE(screen.Modals.TryOpen(kOtherQuestion));

	screen.AskToRemoveTheOnlyProject();

	CHECK_FALSE(ImGuiFrames::IsPopupNamedOpen("Remove from Solution##solutionTreeRemove"));
	CHECK(screen.Modals.HoldsFor(kOtherQuestion));

	screen.Modals.Close(kOtherQuestion);
	screen.Next();

	CHECK(ImGuiFrames::IsPopupNamedOpen("Remove from Solution##solutionTreeRemove"));
}

TEST_CASE("A remove question the solution view withdraws gives the modal gate back and leaves the screen")
{
	TempDirectory folder;
	folder.MakeFile("Tools/Tools.csproj", kProject);
	const std::filesystem::path solution = folder.MakeFile("App.slnx", kOneProjectSolution);

	SolutionScreen screen{solution};

	screen.AskToRemoveTheOnlyProject();
	REQUIRE(ImGuiFrames::IsPopupNamedOpen("Remove from Solution##solutionTreeRemove"));

	screen.View.WithdrawQuestions();
	screen.Next();

	CHECK_FALSE(screen.Modals.IsBusy());
	CHECK_FALSE(ImGuiFrames::IsPopupNamedOpen("Remove from Solution##solutionTreeRemove"));
}

TEST_CASE("Removing a project reloads the solution at once rather than when the folder watcher notices the save")
{
	TempDirectory folder;
	folder.MakeFile("Tools/Tools.csproj", kProject);
	const std::filesystem::path solution = folder.MakeFile("App.slnx", kOneProjectSolution);

	SolutionScreen screen{solution};
	REQUIRE(screen.Workspace.GetLoadedSolution().has_value());
	REQUIRE(screen.Workspace.GetLoadedSolution()->ProjectCount == 1);
	const std::uint64_t loadBeforeTheEdit = screen.Workspace.GetSolutionLoad();

	screen.AskToRemoveTheOnlyProject();
	REQUIRE(ImGuiFrames::IsPopupNamedOpen("Remove from Solution##solutionTreeRemove"));
	screen.Next();
	screen.Frames.PressKey(ImGuiKey_Enter, [&screen] { screen.Draw(); });

	CHECK_FALSE(ImGuiFrames::IsPopupNamedOpen("Remove from Solution##solutionTreeRemove"));
	CHECK(screen.Workspace.GetSolutionLoad() != loadBeforeTheEdit);
	REQUIRE(screen.Workspace.GetLoadedSolution().has_value());
	CHECK(screen.Workspace.GetLoadedSolution()->ProjectCount == 0);
}

TEST_CASE("A new solution folder is listed at once with its name field open")
{
	constexpr int kSolutionRow = 0;
	constexpr int kSolutionDepth = 0;

	TempDirectory folder;
	folder.MakeFile("Tools/Tools.csproj", kProject);
	const std::filesystem::path solution = folder.MakeFile("App.slnx", kOneProjectSolution);

	SolutionScreen screen{solution};
	screen.Frames.Click(SolutionScreen::RowPointCountedFromSolutionRowZero(kSolutionRow, kSolutionDepth),
						[&screen] { screen.Draw(); }, ImGuiMouseButton_Right);
	REQUIRE(screen.Frames.ClickMenuItem("New Folder", [&screen] { screen.Draw(); }));
	screen.Next();
	screen.Next();

	REQUIRE(screen.Workspace.GetLoadedSolution().has_value());
	CHECK(screen.Workspace.GetLoadedSolution()->Root.Folders.size() == 1);
	CHECK(ImGui::GetActiveID() != 0);
}

TEST_CASE("A remove question on screen when the workspace closes gives the modal gate back")
{
	TempDirectory folder;
	folder.MakeFile("Tools/Tools.csproj", kProject);
	const std::filesystem::path solution = folder.MakeFile("App.slnx", kOneProjectSolution);

	SolutionScreen screen{solution};

	screen.AskToRemoveTheOnlyProject();
	REQUIRE(screen.Modals.IsBusy());

	screen.Workspace.Close();
	screen.Next();

	CHECK_FALSE(screen.Modals.IsBusy());
}
