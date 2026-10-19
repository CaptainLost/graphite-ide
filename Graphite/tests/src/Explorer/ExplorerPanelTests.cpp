#include <doctest/doctest.h>

#include "Explorer/ExplorerPanel.h"
#include "Git/GitDecorationView.h"
#include "Git/GitDecorations.h"
#include "Settings/WorkspaceSettings.h"
#include "UI/FileIcons.h"
#include "UI/Lists/ListRow.h"
#include "UI/PanelHeader.h"
#include "Workspace/Workspace.h"

#include "DrawnColours.h"
#include "HoveredItems.h"
#include "ImGuiFrames.h"
#include "TempAppSettings.h"
#include "TempDirectory.h"

#include "Core/Events/EventBus.h"
#include "Core/FileSystem/FileSystem.h"
#include "Core/FileSystem/FileTree.h"
#include "Core/ImGui/Animation.h"
#include "Core/ImGui/Theme.h"
#include "Core/ImGui/UiScale.h"
#include "Core/Text/DocumentSet.h"

#include <imgui.h>
#include <imgui_internal.h>

#include <cmath>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iterator>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
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

			const bool flush = Panel.GetSides() == Core::Panel::Sides::Flush;

			// As the panel manager begins a flush panel's window: with no padding at its sides.
			if (flush)
			{
				ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, ImGui::GetStyle().WindowPadding.y));
			}

			ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
			ImGui::SetNextWindowSize(kExplorerSize);
			ImGui::Begin("Explorer", nullptr,
						 ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
							 ImGuiWindowFlags_NoSavedSettings);

			if (flush)
			{
				ImGui::PopStyleVar();
			}

			FontSize = ImGui::GetFontSize();
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
		float FontSize = 0.0f;
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

namespace {

	constexpr float kHighlightInset = 6.0f;
	constexpr float kPanelMargin = 8.0f;
	constexpr float kLeadLeft = 12.0f;
	constexpr float kLeadWidth = 14.0f;
	constexpr float kLeadToIcon = 5.0f;
	constexpr float kIconSlot = 16.0f;
	constexpr float kIconToName = 5.0f;
	constexpr float kIndentPerLevel = 14.0f;
	constexpr float kInkFringe = 1.0f;
	constexpr float kChevronFringe = 1.5f;
	constexpr ImU32 kModifiedColour = 0xFF3377EEu;
	constexpr std::size_t kModifiedSlot = 1;
	constexpr ImVec4 kGreyText{0.84f, 0.86f, 0.89f, 1.0f};
	constexpr ImVec4 kSeparatorColour{0.9f, 0.1f, 0.6f, 1.0f};

	// As the app's theme lays the Explorer out, with a text colour its emphasis colour stands apart from: ImGui's own
	// text is white, and so is its emphasis.
	void StyleAsTheApp(ExplorerPanelScreen& screen)
	{
		Core::Theme::SetMetrics(ImGui::GetStyle());
		ImGui::GetStyle().Colors[ImGuiCol_Text] = kGreyText;
		screen.Next();
	}

	const ImGuiWindow& ExplorerWindow()
	{
		const ImGuiWindow* window = ImGui::FindWindowByName("Explorer");
		REQUIRE(window != nullptr);
		return *window;
	}

	const ImGuiWindow& TreeBody()
	{
		constexpr std::string_view kTreeBodyName = "##fileTreeBody";
		const ImGuiWindow* body = nullptr;

		for (const ImGuiWindow* window : ImGui::GetCurrentContext()->Windows)
		{
			if (std::string_view{window->Name}.find(kTreeBodyName) != std::string_view::npos)
			{
				body = window;
			}
		}

		REQUIRE(body != nullptr);
		return *body;
	}

	bool ExplorerDrawsInColour(ImU32 colour)
	{
		const ImGuiWindow& explorer = ExplorerWindow();

		for (const ImGuiWindow* window : ImGui::GetCurrentContext()->Windows)
		{
			if (window->RootWindow == &explorer && window->WasActive && DrawsInColour(*window->DrawList, colour))
			{
				return true;
			}
		}

		return false;
	}

	// The ink of a text drawn at x starts its first character's bearing to the right of x.
	float BearingOf(std::string_view text, float size)
	{
		unsigned int codepoint = 0;
		ImTextCharFromUtf8(&codepoint, text.data(), text.data() + text.size());

		const ImFontGlyph* glyph = ImGui::GetFont()->GetFontBaked(size)->FindGlyph(static_cast<ImWchar>(codepoint));
		REQUIRE(glyph != nullptr);
		return glyph->X0;
	}

	class ModifiedFiles
	{
	public:
		ModifiedFiles(const std::filesystem::path& folder, const std::vector<std::u8string>& names)
			: m_Index{StatusOf(names)}
		{
			m_View.Index = &m_Index;
			m_View.SetWorkingDirectory(folder);
			m_View.Colours[kModifiedSlot] = kModifiedColour;
		}

		ModifiedFiles(const ModifiedFiles&) = delete;
		ModifiedFiles& operator=(const ModifiedFiles&) = delete;

		const Graphite::GitDecorationView& View() const { return m_View; }

	private:
		static Core::WorkingTreeStatus StatusOf(const std::vector<std::u8string>& names)
		{
			Core::WorkingTreeStatus status;

			for (const std::u8string& name : names)
			{
				Core::FileStatus modified;
				modified.Path = name;
				modified.WorkTree = Core::StatusFlags::Modified;
				status.Entries.push_back(modified);
			}

			return status;
		}

		Graphite::GitDecorations m_Index;
		Graphite::GitDecorationView m_View;
	};
}

TEST_CASE("The Explorer draws no separator lines")
{
	TempDirectory folder;
	folder.MakeFile("Main.cpp", "int x;");
	folder.MakeFile("Only.slnx", kEmptySolution);

	ExplorerPanelScreen screen{folder.GetPath()};
	StyleAsTheApp(screen);

	// WARNING: ImGui draws its separators in the border colour unless told otherwise, and the window's own border too.
	ImGui::GetStyle().Colors[ImGuiCol_Separator] = kSeparatorColour;
	screen.Next();
	const ImU32 separator = ImGui::GetColorU32(ImGuiCol_Separator);

	CHECK_FALSE(ExplorerDrawsInColour(separator));

	screen.ChooseInDropdown(kModeDropdown, kSolutionOption);
	REQUIRE(screen.Workspace.GetLoadedSolution().has_value());

	CHECK_FALSE(ExplorerDrawsInColour(separator));
}

TEST_CASE("A row's highlight is inset six pixels from the panel's edge")
{
	TempDirectory folder;
	const std::filesystem::path shown = folder.MakeFile("Shown.cpp", "int x;");

	ExplorerPanelScreen screen{folder.GetPath()};
	StyleAsTheApp(screen);
	screen.Panel.SetShownFile(shown);
	screen.Next();

	CHECK(screen.Panel.GetSides() == Core::Panel::Sides::Flush);

	const ImGuiWindow& explorer = ExplorerWindow();
	const std::optional<HorizontalExtent> selected =
		HorizontalExtentInColour(*TreeBody().DrawList, ImGui::GetColorU32(Graphite::CurrentListRowColours().Selected));

	REQUIRE(selected.has_value());
	CHECK(std::abs(selected->Left - (explorer.Pos.x + kHighlightInset)) <= kInkFringe);
	CHECK(std::abs(selected->Right - (explorer.Pos.x + explorer.Size.x - kHighlightInset)) <= kInkFringe);
}

TEST_CASE("The Explorer's header and filter keep eight pixels from the panel's edges")
{
	TempDirectory folder;
	folder.MakeFile("Main.cpp", "int x;");

	ExplorerPanelScreen screen{folder.GetPath()};
	StyleAsTheApp(screen);
	const ImGuiWindow& explorer = ExplorerWindow();
	const float left = explorer.Pos.x;
	const float right = explorer.Pos.x + explorer.Size.x;

	const std::vector<HoveredStretch> header = screen.HeaderItems();
	REQUIRE_FALSE(header.empty());
	CHECK(header.front().From == doctest::Approx(left + kPanelMargin));
	CHECK(header.back().To + 1.0f == doctest::Approx(right - kPanelMargin));

	const std::optional<HorizontalExtent> filter =
		HorizontalExtentInColour(*ExplorerWindow().DrawList, ImGui::GetColorU32(ImGuiCol_FrameBg));
	REQUIRE(filter.has_value());
	CHECK(std::abs(filter->Left - (left + kPanelMargin)) <= kInkFringe);
	CHECK(std::abs(filter->Right - (right - kPanelMargin)) <= kInkFringe);
}

TEST_CASE("A tree row's chevron stands in a box twelve pixels in, its icon five pixels after the box and its name five after the icon")
{
	TempDirectory folder;
	folder.MakeFile("Main.cpp", "int x;");

	ExplorerPanelScreen screen{folder.GetPath()};
	StyleAsTheApp(screen);

	const ImGuiWindow& body = TreeBody();
	const float rowLeft = body.Pos.x;
	const float rowTop = body.DC.CursorStartPos.y;
	const float rowBottom = rowTop + Graphite::ListRowPitch();
	const std::string rootName = Core::ToUtf8(folder.GetPath().filename());

	const std::optional<VerticalExtent> chevronRow =
		VerticalExtentInColour(*body.DrawList, ImGui::GetColorU32(ImGuiCol_TextDisabled));
	REQUIRE(chevronRow.has_value());
	REQUIRE(chevronRow->Top >= rowTop);
	REQUIRE(chevronRow->Bottom <= rowBottom);

	const std::optional<HorizontalExtent> chevron =
		HorizontalExtentInColour(*body.DrawList, ImGui::GetColorU32(ImGuiCol_TextDisabled));
	const std::optional<HorizontalExtent> icon = HorizontalExtentInColour(*body.DrawList, Graphite::DimFolderColour());
	const std::optional<HorizontalExtent> name =
		HorizontalExtentInColour(*body.DrawList, ImGui::GetColorU32(ImGuiCol_Text));

	REQUIRE(chevron.has_value());
	REQUIRE(icon.has_value());
	REQUIRE(name.has_value());

	const float leadLeft = rowLeft + kLeadLeft;
	const float iconLeft = leadLeft + kLeadWidth + kLeadToIcon;
	const float nameLeft = iconLeft + kIconSlot + kIconToName;
	const float folderBearing = BearingOf(Graphite::FolderGlyphOf(true), Core::Scaled(Graphite::kFolderIconSize));

	CHECK(chevron->Left >= leadLeft);
	CHECK(chevron->Right <= leadLeft + kLeadWidth);
	CHECK(std::abs((chevron->Left + chevron->Right) * 0.5f - (leadLeft + kLeadWidth * 0.5f)) <= kChevronFringe);
	CHECK(std::abs(icon->Left - (iconLeft + folderBearing)) <= kInkFringe);
	CHECK(std::abs(name->Left - (nameLeft + BearingOf(rootName, screen.FontSize))) <= kInkFringe);
}

TEST_CASE("A depth-one file's name starts 14 pixels right of a depth-zero one's")
{
	TempDirectory folder;
	folder.MakeFile("Main.cpp", "int x;");
	const ModifiedFiles modified{folder.GetPath(), {u8"Main.cpp"}};

	ExplorerPanelScreen screen{folder.GetPath()};
	StyleAsTheApp(screen);
	screen.Panel.SetGitDecorations(modified.View());
	screen.Next();

	const std::string rootName = Core::ToUtf8(folder.GetPath().filename());
	const std::optional<HorizontalExtent> rootRowName =
		HorizontalExtentInColour(*TreeBody().DrawList, ImGui::GetColorU32(ImGuiCol_Text));
	const std::optional<HorizontalExtent> fileName = HorizontalExtentInColour(*TreeBody().DrawList, kModifiedColour);

	REQUIRE(rootRowName.has_value());
	REQUIRE(fileName.has_value());

	const float depthZeroName = rootRowName->Left - BearingOf(rootName, screen.FontSize);
	const float depthOneName = fileName->Left - BearingOf("Main.cpp", screen.FontSize);

	CHECK(depthOneName - depthZeroName == doctest::Approx(kIndentPerLevel));
}

TEST_CASE("A selected row's name is drawn in the emphasis colour")
{
	TempDirectory folder;
	const std::filesystem::path shown = folder.MakeFile("Shown.cpp", "int x;");

	ExplorerPanelScreen screen{folder.GetPath()};
	StyleAsTheApp(screen);

	CHECK_FALSE(DrawsInColour(*TreeBody().DrawList, Core::EmphasisText()));

	screen.Panel.SetShownFile(shown);
	screen.Next();

	CHECK(DrawsInColour(*TreeBody().DrawList, Core::EmphasisText()));
}

TEST_CASE("A selected row decorated by git keeps its name in the status colour")
{
	TempDirectory folder;
	const std::filesystem::path shown = folder.MakeFile("Shown.cpp", "int x;");
	const ModifiedFiles modified{folder.GetPath(), {u8"Shown.cpp"}};

	ExplorerPanelScreen screen{folder.GetPath()};
	StyleAsTheApp(screen);
	screen.Panel.SetGitDecorations(modified.View());
	screen.Panel.SetShownFile(shown);
	screen.Next();

	CHECK(DrawsInColour(*TreeBody().DrawList, kModifiedColour));
	CHECK_FALSE(DrawsInColour(*TreeBody().DrawList, Core::EmphasisText()));
}
