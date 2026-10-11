#include <doctest/doctest.h>

#include "Explorer/FileTreeView.h"
#include "Workspace/Workspace.h"

#include "ImGuiFrames.h"
#include "TempAppSettings.h"
#include "TempDirectory.h"

#include "Core/Events/EventBus.h"
#include "Core/FileSystem/FileTree.h"
#include "Core/Text/DocumentSet.h"

#include <imgui.h>
#include <imgui_internal.h>

#include <filesystem>
#include <functional>
#include <string>
#include <string_view>
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
		explicit ExplorerScreen(const std::filesystem::path& folder)
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
		Graphite::FileTreeView View{Workspace, Documents, Events, RunJobsAtOnce()};
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
