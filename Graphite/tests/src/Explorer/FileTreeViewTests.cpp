#include <doctest/doctest.h>

#include "Explorer/FileTreeView.h"
#include "Workspace/Workspace.h"

#include "DeferredJobs.h"
#include "DrawnColours.h"
#include "ImGuiFrames.h"
#include "TempAppSettings.h"
#include "TempDirectory.h"

#include "Core/App/Redraw.h"
#include "Core/Events/EventBus.h"
#include "Core/FileSystem/FileTree.h"
#include "Core/Text/DocumentSet.h"

#include <imgui.h>
#include <imgui_internal.h>

#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

	constexpr ImVec2 kExplorerSize{400.0f, 600.0f};

	// Over the filter box: it fills the toolbar's line right of the refresh button.
	constexpr ImVec2 kFilterBox{200.0f, 17.0f};

	constexpr float kTreeIndentForEachLevel = 21.0f;
	constexpr float kIntoTheLabel = 40.0f;

	Core::JobSubmitter RunJobsAtOnce()
	{
		return [](std::function<void()> job) { job(); };
	}

	const ImGuiWindow* FindTreeBody()
	{
		constexpr std::string_view kTreeBodyName = "##fileTreeBody";

		for (const ImGuiWindow* window : ImGui::GetCurrentContext()->Windows)
		{
			if (std::string_view{window->Name}.find(kTreeBodyName) != std::string_view::npos)
			{
				return window;
			}
		}

		return nullptr;
	}

	const Core::FileNode* ChildNamed(const Core::FileNode& folder, const std::string& name)
	{
		for (const Core::FileNode& child : folder.Children)
		{
			if (child.Name == name)
			{
				return &child;
			}
		}

		return nullptr;
	}

	class ExplorerScreen
	{
	public:
		explicit ExplorerScreen(const std::filesystem::path& folder, Core::JobSubmitter submitJob = RunJobsAtOnce())
			: View{Workspace, Documents, Events, std::move(submitJob)}
		{
			REQUIRE(Workspace.OpenFolder(folder));

			Events.Subscribe<Graphite::OpenFileRequested>(
				[this](const Graphite::OpenFileRequested& request) { OpenedFiles.push_back(request.Path); });

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

		// ImGuiFrames steps the clock by a sixtieth of a second a frame.
		void WaitSeconds(double seconds)
		{
			constexpr double kFramesPerSecond = 60.0;
			const int frameCount = static_cast<int>(seconds * kFramesPerSecond) + 1;

			for (int frameIndex = 0; frameIndex < frameCount; ++frameIndex)
			{
				Next();
			}
		}

		void ClearFilterWithEscape()
		{
			Frames.PressKey(ImGuiKey_Escape, [this] { Draw(); });
			Next();
		}

		// Rows are counted from the top of the tree, the root's row being 0.
		void ClickRow(int rowIndex, int depth)
		{
			const ImGuiWindow* body = FindTreeBody();
			REQUIRE(body != nullptr);

			const float rowPitch = ImGui::GetFontSize() + ImGui::GetStyle().ItemSpacing.y;
			const float x = body->DC.CursorStartPos.x + static_cast<float>(depth) * kTreeIndentForEachLevel + kIntoTheLabel;
			const float y = body->DC.CursorStartPos.y + static_cast<float>(rowIndex) * rowPitch + ImGui::GetFontSize() * 0.5f;

			Click(ImVec2(x, y));
		}

		ImGuiFrames Frames;
		TempAppSettings Settings;
		Core::EventBus Events;
		Graphite::Workspace Workspace{Events, Settings.Get()};
		Core::DocumentSet Documents;
		Graphite::FileTreeView View;
		std::vector<std::filesystem::path> OpenedFiles;
	};
}

TEST_CASE("The filter finds a file in a folder nobody opened, and a click on it opens the file")
{
	TempDirectory folder;
	const std::filesystem::path deep = folder.MakeFile("src/Engine/Deep.cpp");
	folder.MakeFile("src/Other.cpp");
	folder.MakeFile("README.md");

	ExplorerScreen screen{folder.GetPath()};
	screen.TypeInFilter("Deep");

	const int deepRow = 3;
	const int deepDepth = 3;
	screen.ClickRow(deepRow, deepDepth);

	REQUIRE(screen.OpenedFiles.size() == 1);
	CHECK(std::filesystem::equivalent(screen.OpenedFiles[0], deep));
}

TEST_CASE("Clearing the filter leaves closed the folders it opened")
{
	TempDirectory folder;
	folder.MakeFile("src/Engine/Deep.cpp");

	ExplorerScreen screen{folder.GetPath()};
	screen.TypeInFilter("Deep");
	screen.ClearFilterWithEscape();

	const Core::FileNode* source = ChildNamed(screen.Workspace.GetTree().GetRoot(), "src");
	REQUIRE(source != nullptr);
	CHECK_FALSE(source->Expanded);
}

TEST_CASE("The file tree marks the active file only while its tab is the one in front in the editor")
{
	TempDirectory folder;
	folder.MakeFile("Shown.cpp", "int x;");

	ExplorerScreen screen{folder.GetPath()};
	const Core::FileNode* file = ChildNamed(screen.Workspace.GetTree().GetRoot(), "Shown.cpp");
	REQUIRE(file != nullptr);
	const std::filesystem::path filePath = file->Path;
	REQUIRE(screen.Documents.Open(filePath, Core::TabKind::Permanent) == Core::OpenOutcome::Opened);

	const std::filesystem::path noFileInFront;
	screen.View.SetShownFile(noFileInFront);
	screen.Next();

	const ImGuiWindow* body = FindTreeBody();
	REQUIRE(body != nullptr);
	CHECK_FALSE(MarksARow(*body->DrawList));

	screen.View.SetShownFile(filePath);
	screen.Next();

	body = FindTreeBody();
	REQUIRE(body != nullptr);
	CHECK(MarksARow(*body->DrawList));
}

TEST_CASE("The file tree marks a file in the preview tab with a ring and one in a permanent tab with a dot")
{
	TempDirectory folder;
	folder.MakeFile("Previewed.cpp", "int x;");

	ExplorerScreen screen{folder.GetPath()};
	const Core::FileNode* file = ChildNamed(screen.Workspace.GetTree().GetRoot(), "Previewed.cpp");
	REQUIRE(file != nullptr);
	const std::filesystem::path filePath = file->Path;

	// The default check mark shares its colour with the selection and the slider grabs.
	ImGui::GetStyle().Colors[ImGuiCol_CheckMark] = ImVec4(0.9f, 0.4f, 0.1f, 1.0f);
	const ImU32 openTabMark = ImGui::GetColorU32(ImGuiCol_CheckMark);

	screen.Next();
	const ImGuiWindow* body = FindTreeBody();
	REQUIRE(body != nullptr);
	CHECK_FALSE(DrawsInColour(*body->DrawList, openTabMark));

	REQUIRE(screen.Documents.Open(filePath, Core::TabKind::Preview) == Core::OpenOutcome::Opened);
	screen.Next();
	body = FindTreeBody();
	REQUIRE(body != nullptr);
	CHECK(DrawsInColour(*body->DrawList, openTabMark));
	CHECK_FALSE(FillsMiddleInColour(*body->DrawList, openTabMark));

	const std::optional<std::size_t> previewIndex = screen.Documents.Find(filePath);
	REQUIRE(previewIndex.has_value());
	screen.Documents.Promote(*previewIndex);
	screen.Next();
	body = FindTreeBody();
	REQUIRE(body != nullptr);
	CHECK(FillsMiddleInColour(*body->DrawList, openTabMark));
}

TEST_CASE("While the first read of the folder is new, the file tree draws nothing and wakes itself when the wait is over")
{
	TempDirectory folder;
	folder.MakeFile("src/Deep.cpp");

	DeferredJobs jobs;
	ExplorerScreen screen{folder.GetPath(), jobs.Submitter()};
	screen.TypeInFilter("Deep");
	static_cast<void>(Core::Redraw::TakeDeadline());

	screen.Next();

	const std::optional<Core::Redraw::Clock::time_point> wake = Core::Redraw::TakeDeadline();
	REQUIRE(wake.has_value());
	CHECK(*wake > Core::Redraw::Clock::now());
}

TEST_CASE("A read of the folder that takes a while is shown by a moving loader until it finishes")
{
	TempDirectory folder;
	folder.MakeFile("src/Deep.cpp");

	DeferredJobs jobs;
	ExplorerScreen screen{folder.GetPath(), jobs.Submitter()};
	screen.TypeInFilter("Deep");

	const double pastTheWait = 0.25;
	screen.WaitSeconds(pastTheWait);
	static_cast<void>(Core::Redraw::TakeDeadline());

	screen.Next();

	const std::optional<Core::Redraw::Clock::time_point> nextFrame = Core::Redraw::TakeDeadline();
	REQUIRE(nextFrame.has_value());
	CHECK(*nextFrame <= Core::Redraw::Clock::now());

	jobs.RunAll();
	screen.Next();
	static_cast<void>(Core::Redraw::TakeDeadline());

	screen.Next();

	CHECK_FALSE(Core::Redraw::TakeDeadline().has_value());
}
