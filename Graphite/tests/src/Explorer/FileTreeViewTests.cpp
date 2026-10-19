#include <doctest/doctest.h>

#include "Explorer/FileTreeView.h"
#include "Git/GitDecorationView.h"
#include "Git/GitDecorations.h"
#include "UI/FileIcons.h"
#include "UI/Lists/Chevron.h"
#include "UI/Lists/ListRow.h"
#include "Workspace/Workspace.h"

#include "DeferredJobs.h"
#include "DrawnColours.h"
#include "ImGuiFrames.h"
#include "TempAppSettings.h"
#include "TempDirectory.h"

#include "Core/App/Redraw.h"
#include "Core/Events/EventBus.h"
#include "Core/FileSystem/FileTree.h"
#include "Core/ImGui/ImGuiColors.h"
#include "Core/Text/DocumentSet.h"

#include <imgui.h>
#include <imgui_internal.h>

#include <cmath>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

	constexpr ImVec2 kExplorerSize{400.0f, 600.0f};

	constexpr ImVec2 kFilterBox{200.0f, 17.0f};

	constexpr float kTreeIndentForEachLevel = 21.0f;
	constexpr float kIntoTheLabel = 40.0f;

	constexpr int kDoomedRow = 1;
	constexpr int kDoomedDepth = 1;

	constexpr int kRootRow = 0;
	constexpr int kRootDepth = 0;

	constexpr const char* kNewFileTemplateMenuItem = "New File from Template\xE2\x80\xA6";
	constexpr const char* kNewFileTemplatePopup = "New File from Template##fileTreeNewFile";

	std::size_t EntriesIn(const std::filesystem::path& folder)
	{
		return static_cast<std::size_t>(std::distance(std::filesystem::directory_iterator{folder}, std::filesystem::directory_iterator{}));
	}

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

	Core::FileNode* ChildNamedToChange(Core::FileNode& folder, const std::string& name)
	{
		for (Core::FileNode& child : folder.Children)
		{
			if (child.Name == name)
			{
				return &child;
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
			: View{Workspace, Documents, Events, Modals, Scheme, std::move(submitJob)}
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

		static ImVec2 RowPoint(int rowIndexFromRoot, int depth)
		{
			const ImGuiWindow* body = FindTreeBody();
			REQUIRE(body != nullptr);

			const float rowPitch = Graphite::ListRowPitch();
			const float x = body->DC.CursorStartPos.x + static_cast<float>(depth) * kTreeIndentForEachLevel + kIntoTheLabel;
			const float y = body->DC.CursorStartPos.y + static_cast<float>(rowIndexFromRoot) * rowPitch + ImGui::GetFontSize() * 0.5f;

			return ImVec2(x, y);
		}

		void ClickRow(int rowIndexFromRoot, int depth)
		{
			Click(RowPoint(rowIndexFromRoot, depth));
		}

		void RightClickRow(int rowIndexFromRoot, int depth)
		{
			Frames.Click(RowPoint(rowIndexFromRoot, depth), [this] { Draw(); }, ImGuiMouseButton_Right);
		}

		void AskToDeleteRow(int rowIndexFromRoot, int depth)
		{
			RightClickRow(rowIndexFromRoot, depth);
			REQUIRE(ChooseMenuItem("Delete"));
			Next();
		}

		bool ChooseMenuItem(const char* label)
		{
			return Frames.ClickMenuItem(label, [this] { Draw(); });
		}

		void ChooseFromRootMenu(const char* label)
		{
			RightClickRow(kRootRow, kRootDepth);
			REQUIRE(ChooseMenuItem(label));
			Next();
			Next();
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

		ImGuiFrames Frames;
		TempAppSettings Settings;
		Core::EventBus Events;
		Graphite::Workspace Workspace{Events, Settings.Get()};
		Core::DocumentSet Documents;
		Graphite::ModalGate Modals;
		Graphite::ActiveScheme Scheme;
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

TEST_CASE("A filter that matches nothing says so, starting where a tree row's chevron does and ending at a row's end")
{
	TempDirectory folder;
	folder.MakeFile("README.md");

	ExplorerScreen screen{folder.GetPath()};
	screen.TypeInFilter("nothing by this name, nothing at all, not anywhere in this folder");

	const ImGuiWindow* body = FindTreeBody();
	REQUIRE(body != nullptr);

	const std::optional<HorizontalExtent> message =
		HorizontalExtentInColour(*body->DrawList, ImGui::GetColorU32(ImGuiCol_TextDisabled));

	REQUIRE(message.has_value());
	CHECK(message->Left >= body->Pos.x + Graphite::TreeRowChevronBoxLeft());
	CHECK(message->Right <= body->Pos.x + body->Size.x - Graphite::ListRowEndInset());
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

TEST_CASE("The file tree's filter field spans the panel's width with nothing after it")
{
	constexpr float kFringe = 1.0f;

	TempDirectory folder;
	folder.MakeFile("README.md");

	ExplorerScreen screen{folder.GetPath()};

	const ImGuiWindow* explorer = ImGui::FindWindowByName("Explorer");
	REQUIRE(explorer != nullptr);

	const std::optional<HorizontalExtent> field =
		HorizontalExtentInColour(*explorer->DrawList, ImGui::GetColorU32(ImGuiCol_FrameBg));
	REQUIRE(field.has_value());
	CHECK(std::abs(field->Left - (explorer->Pos.x + explorer->WindowPadding.x)) <= kFringe);
	CHECK(std::abs(field->Right - (explorer->Pos.x + explorer->Size.x - explorer->WindowPadding.x)) <= kFringe);
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

TEST_CASE("An Explorer file row draws its badge in its kind's colour and its name in the status colour")
{
	constexpr ImU32 kModifiedColour = 0xFF3377EEu;
	constexpr std::size_t kModifiedSlot = 1;
	constexpr bool kDarkScheme = false;

	TempDirectory folder;
	folder.MakeFile("Main.cpp", "int x;");

	ExplorerScreen screen{folder.GetPath()};

	Core::FileStatus modified;
	modified.Path = u8"Main.cpp";
	modified.WorkTree = Core::StatusFlags::Modified;

	Core::WorkingTreeStatus status;
	status.Entries.push_back(modified);
	const Graphite::GitDecorations index{status};

	Graphite::GitDecorationView decorations;
	decorations.Index = &index;
	decorations.SetWorkingDirectory(folder.GetPath());
	decorations.Colours[kModifiedSlot] = kModifiedColour;

	screen.View.SetGitDecorations(decorations);
	screen.Next();

	const ImGuiWindow* body = FindTreeBody();
	REQUIRE(body != nullptr);

	const std::optional<HorizontalExtent> glyph =
		HorizontalExtentInColour(*body->DrawList, Graphite::ColourOf(Graphite::FileKind::CppSource, kDarkScheme));
	const std::optional<HorizontalExtent> name = HorizontalExtentInColour(*body->DrawList, kModifiedColour);

	REQUIRE(glyph.has_value());
	REQUIRE(name.has_value());
	CHECK(glyph->Right < name->Left);
}

TEST_CASE("An ignored Explorer file row draws its badge at half its kind's alpha")
{
	constexpr bool kDarkScheme = false;
	constexpr float kHalfAlpha = 0.5f;

	TempDirectory folder;
	folder.MakeFile("build.json", "{}");

	ExplorerScreen screen{folder.GetPath()};
	Core::FileNode* file = ChildNamedToChange(screen.Workspace.GetTree().GetRoot(), "build.json");
	REQUIRE(file != nullptr);

	const ImU32 kindColour = Graphite::ColourOf(Graphite::FileKind::Json, kDarkScheme);

	screen.Next();
	const ImGuiWindow* body = FindTreeBody();
	REQUIRE(body != nullptr);
	REQUIRE(DrawsInColour(*body->DrawList, kindColour));

	file->Ignored = true;
	screen.Next();
	body = FindTreeBody();
	REQUIRE(body != nullptr);
	CHECK(DrawsInColour(*body->DrawList, Core::WithAlphaScaled(kindColour, kHalfAlpha)));
	CHECK_FALSE(DrawsInColour(*body->DrawList, kindColour));
}

TEST_CASE("An Explorer folder row draws the outline glyph in the folder grey")
{
	TempDirectory folder;
	folder.MakeFile("src/Main.cpp", "int x;");

	ExplorerScreen screen{folder.GetPath()};
	screen.Next();

	const ImGuiWindow* body = FindTreeBody();
	REQUIRE(body != nullptr);
	CHECK(DrawsInColour(*body->DrawList, Graphite::DimFolderColour()));
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

TEST_CASE("Delete chosen while another question holds the modal gate waits for it, and asks once the gate is free")
{
	TempDirectory folder;
	folder.MakeFile("doomed.txt");

	ExplorerScreen screen{folder.GetPath()};
	constexpr std::string_view kOtherQuestion = "Another question";
	REQUIRE(screen.Modals.TryOpen(kOtherQuestion));

	screen.AskToDeleteRow(kDoomedRow, kDoomedDepth);

	CHECK_FALSE(ImGuiFrames::IsPopupNamedOpen("Delete##fileTreeDelete"));
	CHECK(screen.Modals.HoldsFor(kOtherQuestion));

	screen.Modals.Close(kOtherQuestion);
	screen.Next();

	CHECK(ImGuiFrames::IsPopupNamedOpen("Delete##fileTreeDelete"));
}

TEST_CASE("A delete question the file tree withdraws gives the modal gate back and leaves the screen")
{
	TempDirectory folder;
	folder.MakeFile("doomed.txt");

	ExplorerScreen screen{folder.GetPath()};

	screen.AskToDeleteRow(kDoomedRow, kDoomedDepth);
	REQUIRE(ImGuiFrames::IsPopupNamedOpen("Delete##fileTreeDelete"));

	screen.View.WithdrawQuestions();
	screen.Next();

	CHECK_FALSE(screen.Modals.IsBusy());
	CHECK_FALSE(ImGuiFrames::IsPopupNamedOpen("Delete##fileTreeDelete"));
	CHECK(std::filesystem::exists(folder.GetPath() / "doomed.txt"));
}

TEST_CASE("A delete question on screen when the folder closes gives the modal gate back")
{
	TempDirectory folder;
	folder.MakeFile("doomed.txt");

	ExplorerScreen screen{folder.GetPath()};

	screen.AskToDeleteRow(kDoomedRow, kDoomedDepth);
	REQUIRE(screen.Modals.IsBusy());

	screen.Workspace.Close();
	screen.Next();

	CHECK_FALSE(screen.Modals.IsBusy());
}

TEST_CASE("Enter does not answer the delete question, so nothing is moved to the Recycle Bin by it")
{
	TempDirectory folder;
	folder.MakeFile("doomed.txt");

	ExplorerScreen screen{folder.GetPath()};

	screen.AskToDeleteRow(kDoomedRow, kDoomedDepth);
	REQUIRE(ImGuiFrames::IsPopupNamedOpen("Delete##fileTreeDelete"));

	screen.Frames.PressKey(ImGuiKey_Enter, [&screen] { screen.Draw(); });
	screen.Next();

	CHECK(std::filesystem::exists(folder.GetPath() / "doomed.txt"));
}

TEST_CASE("New File makes nothing on disk until the name is entered")
{
	TempDirectory folder;
	folder.MakeFile("existing.txt");

	ExplorerScreen screen{folder.GetPath()};

	screen.ChooseFromRootMenu("New File");

	CHECK(EntriesIn(folder.GetPath()) == 1);
}

TEST_CASE("Escape in the New File name field leaves the folder as it was")
{
	TempDirectory folder;
	folder.MakeFile("existing.txt");

	ExplorerScreen screen{folder.GetPath()};

	screen.ChooseFromRootMenu("New File");
	screen.Type("draft");
	screen.Press(ImGuiKey_Escape);

	CHECK(EntriesIn(folder.GetPath()) == 1);
	CHECK(screen.OpenedFiles.empty());
}

TEST_CASE("Enter in the New File name field makes the file and opens it")
{
	TempDirectory folder;
	folder.MakeFile("existing.txt");

	ExplorerScreen screen{folder.GetPath()};

	screen.ChooseFromRootMenu("New File");
	screen.Type("Foo.cpp");
	screen.Press(ImGuiKey_Enter);

	CHECK(std::filesystem::is_regular_file(folder.GetPath() / "Foo.cpp"));
	REQUIRE(screen.OpenedFiles.size() == 1);
	CHECK(screen.OpenedFiles.front() == folder.GetPath() / "Foo.cpp");
}

TEST_CASE("A name with slashes in the New File field makes the folders on the way")
{
	TempDirectory folder;

	ExplorerScreen screen{folder.GetPath()};

	screen.ChooseFromRootMenu("New File");
	screen.Type("util/text/Strings.cpp");
	screen.Press(ImGuiKey_Enter);

	CHECK(std::filesystem::is_regular_file(folder.GetPath() / "util" / "text" / "Strings.cpp"));
}

TEST_CASE("A New File name that is taken keeps the field open and leaves the old file alone")
{
	TempDirectory folder;
	folder.MakeFile("taken.txt");

	ExplorerScreen screen{folder.GetPath()};

	screen.ChooseFromRootMenu("New File");
	screen.Type("taken.txt");
	screen.Press(ImGuiKey_Enter);
	screen.Next();

	CHECK(EntriesIn(folder.GetPath()) == 1);
	CHECK(screen.OpenedFiles.empty());

	screen.Press(ImGuiKey_Escape);

	CHECK(EntriesIn(folder.GetPath()) == 1);
}

TEST_CASE("New File from Template opens a dialog that makes nothing until Create")
{
	TempDirectory folder;

	ExplorerScreen screen{folder.GetPath()};

	screen.ChooseFromRootMenu(kNewFileTemplateMenuItem);

	CHECK(ImGuiFrames::IsPopupNamedOpen(kNewFileTemplatePopup));
	CHECK(EntriesIn(folder.GetPath()) == 0);
}

TEST_CASE("Enter in the template dialog makes the file and closes the dialog")
{
	TempDirectory folder;

	ExplorerScreen screen{folder.GetPath()};

	screen.ChooseFromRootMenu(kNewFileTemplateMenuItem);
	screen.Type("notes.md");
	screen.Press(ImGuiKey_Enter);
	screen.Next();

	CHECK(std::filesystem::is_regular_file(folder.GetPath() / "notes.md"));
	CHECK_FALSE(ImGuiFrames::IsPopupNamedOpen(kNewFileTemplatePopup));
	CHECK_FALSE(screen.Modals.IsBusy());
}

TEST_CASE("Escape closes the template dialog and makes nothing")
{
	TempDirectory folder;

	ExplorerScreen screen{folder.GetPath()};

	screen.ChooseFromRootMenu(kNewFileTemplateMenuItem);
	screen.Type("notes.md");
	screen.Press(ImGuiKey_Escape);
	screen.Next();

	CHECK(EntriesIn(folder.GetPath()) == 0);
	CHECK_FALSE(ImGuiFrames::IsPopupNamedOpen(kNewFileTemplatePopup));
	CHECK_FALSE(screen.Modals.IsBusy());
}

TEST_CASE("The template dialog refuses a name that is already taken")
{
	TempDirectory folder;
	folder.MakeFile("taken.txt");

	ExplorerScreen screen{folder.GetPath()};

	screen.ChooseFromRootMenu(kNewFileTemplateMenuItem);
	screen.Type("taken.txt");
	screen.Press(ImGuiKey_Enter);

	CHECK(ImGuiFrames::IsPopupNamedOpen(kNewFileTemplatePopup));
	CHECK(EntriesIn(folder.GetPath()) == 1);
}
