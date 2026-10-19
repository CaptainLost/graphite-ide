#include <doctest/doctest.h>

#include "Explorer/ExplorerPanel.h"
#include "Settings/WorkspaceSettings.h"
#include "UI/PanelHeader.h"
#include "Workspace/Workspace.h"

#include "HoveredItems.h"
#include "ImGuiFrames.h"
#include "TempAppSettings.h"
#include "TempDirectory.h"

#include "Core/Events/EventBus.h"
#include "Core/FileSystem/FileTree.h"
#include "Core/ImGui/Animation.h"
#include "Core/Text/DocumentSet.h"

#include <imgui.h>
#include <imgui_internal.h>

#include <cstddef>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iterator>
#include <sstream>
#include <string>
#include <vector>

namespace {

	constexpr ImVec2 kExplorerSize{400.0f, 600.0f};
	constexpr float kHalf = 0.5f;

	constexpr std::size_t kModeDropdown = 0;
	constexpr std::size_t kSolutionDropdown = 1;
	constexpr int kFileSystemOption = 0;
	constexpr int kSolutionOption = 1;

	constexpr const char* kModeKey = "explorerMode";

	constexpr const char* kEmptySolution = R"(<Solution>
</Solution>
)";

	Core::JobSubmitter RunJobsAtOnce()
	{
		return [](std::function<void()> job) { job(); };
	}

	std::size_t EntriesIn(const std::filesystem::path& folder)
	{
		return static_cast<std::size_t>(
			std::distance(std::filesystem::directory_iterator{folder}, std::filesystem::directory_iterator{}));
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

	std::string ReadWhole(const std::filesystem::path& path)
	{
		std::ifstream stream{path, std::ios::binary};
		std::ostringstream contents;
		contents << stream.rdbuf();
		return contents.str();
	}

	class ExplorerPanelScreen
	{
	public:
		explicit ExplorerPanelScreen(const std::filesystem::path& folder)
		{
			REQUIRE(Workspace.OpenFolder(folder));

			Events.Subscribe<Graphite::OpenFileRequested>(
				[this](const Graphite::OpenFileRequested& request) { OpenedFiles.push_back(request.Path); });

			Next();
			Next();
		}

		void Draw()
		{
			Core::Animation::BeginFrame();

			ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
			ImGui::SetNextWindowSize(kExplorerSize);
			ImGui::Begin("Explorer", nullptr,
						 ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
							 ImGuiWindowFlags_NoSavedSettings);

			HeaderTop = ImGui::GetCursorScreenPos();
			ContentRight = HeaderTop.x + ImGui::GetContentRegionAvail().x;
			Panel.OnImGuiRender();

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

		void Type(const char* text)
		{
			ImGui::GetIO().AddInputCharactersUTF8(text);
			Next();
		}

		void Press(ImGuiKey key)
		{
			Frames.PressKey(key, [this] { Draw(); });
			Next();
		}

		float HeaderMiddle() const { return HeaderTop.y + Graphite::PanelHeaderHeight() * kHalf; }

		std::vector<HoveredStretch> HeaderItems()
		{
			return HoveredAcross(Frames, HeaderMiddle(), HeaderTop.x, ContentRight, [this] { Draw(); });
		}

		void ClickHeaderItem(std::size_t index)
		{
			const std::vector<HoveredStretch> items = HeaderItems();
			REQUIRE(index < items.size());
			Click(ImVec2(items[index].Middle(), HeaderMiddle()));
			Next();
			Next();
		}

		void ChooseInDropdown(std::size_t dropdown, int option)
		{
			const std::vector<HoveredStretch> items = HeaderItems();
			REQUIRE(dropdown < items.size());
			Click(ImVec2(items[dropdown].Middle(), HeaderMiddle()));
			REQUIRE(Frames.ClickPopupRow(option, [this] { Draw(); }));
			Next();
			Next();
		}

		ImGuiFrames Frames;
		TempAppSettings Settings;
		Core::EventBus Events;
		Graphite::Workspace Workspace{Events, Settings.Get()};
		Core::DocumentSet Documents;
		Graphite::ModalGate Modals;
		Graphite::ActiveScheme Scheme;
		Graphite::ExplorerPanel Panel{Workspace, Documents, Settings.Get(), Events, Modals, Scheme, RunJobsAtOnce()};
		std::vector<std::filesystem::path> OpenedFiles;
		ImVec2 HeaderTop;
		float ContentRight = 0.0f;
	};

	constexpr std::size_t kNewFileButton = 1;
	constexpr std::size_t kNewFolderButton = 2;
	constexpr std::size_t kRefreshButton = 3;
	constexpr std::size_t kSolutionNewFolderButton = 1;
}

TEST_CASE("The Explorer's header holds the view's dropdown and New File, New Folder and Refresh over the files")
{
	TempDirectory folder;
	folder.MakeFile("existing.txt");

	ExplorerPanelScreen screen{folder.GetPath()};

	CHECK(screen.HeaderItems().size() == 4);
}

TEST_CASE("New File in the Explorer's header makes nothing until the name is entered, then makes the file at the root")
{
	TempDirectory folder;
	folder.MakeFile("existing.txt");

	ExplorerPanelScreen screen{folder.GetPath()};
	screen.ClickHeaderItem(kNewFileButton);

	CHECK(EntriesIn(folder.GetPath()) == 1);

	screen.Type("Foo.cpp");
	screen.Press(ImGuiKey_Enter);

	CHECK(std::filesystem::is_regular_file(folder.GetPath() / "Foo.cpp"));
	REQUIRE(screen.OpenedFiles.size() == 1);
	CHECK(screen.OpenedFiles.front() == folder.GetPath() / "Foo.cpp");
}

TEST_CASE("New Folder in the Explorer's header makes a folder at the root")
{
	TempDirectory folder;
	folder.MakeFile("existing.txt");

	ExplorerPanelScreen screen{folder.GetPath()};
	screen.ClickHeaderItem(kNewFolderButton);

	CHECK(std::filesystem::is_directory(folder.GetPath() / "new folder"));
}

TEST_CASE("Refresh in the Explorer's header reads in a file made outside the application")
{
	TempDirectory folder;
	folder.MakeFile("existing.txt");

	ExplorerPanelScreen screen{folder.GetPath()};
	folder.MakeFile("late.txt");
	screen.Next();

	REQUIRE(ChildNamed(screen.Workspace.GetTree().GetRoot(), "late.txt") == nullptr);

	screen.ClickHeaderItem(kRefreshButton);

	CHECK(ChildNamed(screen.Workspace.GetTree().GetRoot(), "late.txt") != nullptr);
}

TEST_CASE("The view chosen in the Explorer's header is kept as the folder's explorer mode")
{
	TempDirectory folder;
	folder.MakeFile("existing.txt");

	ExplorerPanelScreen screen{folder.GetPath()};

	screen.ChooseInDropdown(kModeDropdown, kSolutionOption);
	CHECK(Graphite::WorkspaceSettings{folder.GetPath()}.Read(kModeKey) == "solution");

	screen.ChooseInDropdown(kModeDropdown, kFileSystemOption);
	CHECK(Graphite::WorkspaceSettings{folder.GetPath()}.Read(kModeKey) == "files");
}

TEST_CASE("Over a solution the Explorer's header holds New Folder and Refresh, and New Folder adds a solution folder")
{
	TempDirectory folder;
	const std::filesystem::path solution = folder.MakeFile("Only.slnx", kEmptySolution);

	ExplorerPanelScreen screen{folder.GetPath()};
	screen.ChooseInDropdown(kModeDropdown, kSolutionOption);

	REQUIRE(screen.HeaderItems().size() == 3);

	screen.ClickHeaderItem(kSolutionNewFolderButton);

	CHECK(ReadWhole(solution).find("New Folder") != std::string::npos);
}

TEST_CASE("With two solutions in the folder, the Explorer's header chooses between them")
{
	TempDirectory folder;
	folder.MakeFile("First.slnx", kEmptySolution);
	folder.MakeFile("Second.slnx", kEmptySolution);

	ExplorerPanelScreen screen{folder.GetPath()};
	screen.ChooseInDropdown(kModeDropdown, kSolutionOption);

	REQUIRE(screen.HeaderItems().size() == 4);

	const std::vector<std::filesystem::path> candidates = screen.Workspace.GetSolutionCandidates();
	REQUIRE(candidates.size() == 2);
	REQUIRE(screen.Workspace.GetLoadedSolution().has_value());

	const int other = candidates[0] == screen.Workspace.GetLoadedSolution()->Path ? 1 : 0;
	screen.ChooseInDropdown(kSolutionDropdown, other);

	REQUIRE(screen.Workspace.GetLoadedSolution().has_value());
	CHECK(screen.Workspace.GetLoadedSolution()->Path == candidates[static_cast<std::size_t>(other)]);
}
