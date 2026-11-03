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
#include "Core/Git/Repository.h"
#include "Core/ImGui/ImGuiColors.h"
#include "Core/Text/DocumentSet.h"

#include <imgui.h>
#include <imgui_internal.h>

#include <chrono>
#include <cmath>
#include <condition_variable>
#include <filesystem>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
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
		explicit ExplorerScreen(const std::filesystem::path& folder, Core::JobSubmitter submitJob = RunJobsAtOnce(),
								Graphite::FileOperationRunner runFileOperation = Graphite::RunFileOperation)
			: View{Workspace, Documents, Events, Modals, Scheme, std::move(submitJob), std::move(runFileOperation)}
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

		// WARNING: A disabled item still takes the hover, only flagged as disabled.
		std::optional<bool> IsMenuItemEnabled(const char* label)
		{
			constexpr float kStepDown = 4.0f;
			constexpr float kIntoTheMenuLabel = 8.0f;

			const ImGuiContext& context = *ImGui::GetCurrentContext();

			if (context.OpenPopupStack.Size == 0 || context.OpenPopupStack.back().Window == nullptr)
			{
				return std::nullopt;
			}

			ImGuiWindow& menu = *context.OpenPopupStack.back().Window;
			const ImGuiID itemId = menu.GetID(label);
			const float x = menu.Pos.x + menu.WindowPadding.x + kIntoTheMenuLabel;

			for (float y = menu.Pos.y; y < menu.Pos.y + menu.Size.y; y += kStepDown)
			{
				ImGui::GetIO().AddMousePosEvent(x, y);
				Next();

				if (context.HoveredId == itemId)
				{
					return !context.HoveredIdIsDisabled;
				}
			}

			return std::nullopt;
		}

		// WARNING: ImGui keeps no item's position past its frame, so the pointer has to search for the button.
		bool ClickLastDialogButton(const char* label)
		{
			constexpr float kStepUp = 3.0f;
			constexpr float kIntoTheButton = 12.0f;

			const ImGuiContext& context = *ImGui::GetCurrentContext();

			if (context.OpenPopupStack.Size == 0 || context.OpenPopupStack.back().Window == nullptr)
			{
				return false;
			}

			ImGuiWindow& dialog = *context.OpenPopupStack.back().Window;
			const ImGuiID buttonId = dialog.GetID(label);
			const float x = dialog.Pos.x + dialog.Size.x - dialog.WindowPadding.x - kIntoTheButton;

			for (float y = dialog.Pos.y + dialog.Size.y; y > dialog.Pos.y; y -= kStepUp)
			{
				ImGui::GetIO().AddMousePosEvent(x, y);
				Next();

				if (context.HoveredId == buttonId)
				{
					Click(ImVec2(x, y));
					return true;
				}
			}

			return false;
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

TEST_CASE("Focusing the filter starts reading the folder before a letter is typed")
{
	TempDirectory folder;
	folder.MakeFile("src/Deep.cpp");

	DeferredJobs jobs;
	ExplorerScreen screen{folder.GetPath(), jobs.Submitter()};
	screen.Next();

	CHECK(jobs.Count() == 0);

	screen.Click(kFilterBox);
	screen.Next();

	CHECK(jobs.Count() == 1);
}

TEST_CASE("A change on disk is not read again until the filter is focused")
{
	TempDirectory folder;
	folder.MakeFile("src/Deep.cpp");

	DeferredJobs jobs;
	ExplorerScreen screen{folder.GetPath(), jobs.Submitter()};
	screen.Click(kFilterBox);
	jobs.RunAll();
	screen.ClearFilterWithEscape();
	screen.Click(ImVec2(kExplorerSize.x * 0.5f, kExplorerSize.y - 20.0f));
	screen.Next();

	screen.Events.Publish(Graphite::WorkspaceFilesChangedEvent{.Everything = true});
	screen.Next();
	screen.Next();

	CHECK(jobs.Count() == 0);
}

TEST_CASE("Matching files stay where they are through a quick re-read and make room for its loader only once it shows")
{
	TempDirectory folder;
	folder.MakeFile("src/Deep.cpp");

	DeferredJobs jobs;
	ExplorerScreen screen{folder.GetPath(), jobs.Submitter()};
	screen.Click(kFilterBox);
	jobs.RunAll();
	screen.Type("Deep");
	screen.Next();
	REQUIRE(FindTreeBody() != nullptr);
	const float resultsHeight = FindTreeBody()->ContentSize.y;

	screen.Events.Publish(Graphite::WorkspaceFilesChangedEvent{.Everything = true});
	screen.Next();
	screen.Next();
	REQUIRE(jobs.Count() == 1);

	CHECK(FindTreeBody()->ContentSize.y == doctest::Approx(resultsHeight));

	const double pastTheWait = 0.25;
	screen.WaitSeconds(pastTheWait);

	CHECK(FindTreeBody()->ContentSize.y > resultsHeight);

	jobs.RunAll();
	screen.Next();
	screen.Next();

	CHECK(FindTreeBody()->ContentSize.y == doctest::Approx(resultsHeight));
}

TEST_CASE("A filter matching nothing keeps its message for the first fifth of a second of a re-read, then wraps the loader's")
{
	TempDirectory folder;
	folder.MakeFile("README.md");

	DeferredJobs jobs;
	ExplorerScreen screen{folder.GetPath(), jobs.Submitter()};
	screen.Click(kFilterBox);
	jobs.RunAll();
	screen.Type("nothing-by-this-name");
	screen.Next();

	const auto messageExtent = [] {
		const ImGuiWindow* body = FindTreeBody();
		REQUIRE(body != nullptr);
		return HorizontalExtentInColour(*body->DrawList, ImGui::GetColorU32(ImGuiCol_TextDisabled));
	};

	const std::optional<HorizontalExtent> answered = messageExtent();
	REQUIRE(answered.has_value());

	screen.Events.Publish(Graphite::WorkspaceFilesChangedEvent{.Everything = true});
	screen.Next();
	REQUIRE(jobs.Count() == 1);

	const std::optional<HorizontalExtent> rereading = messageExtent();
	REQUIRE(rereading.has_value());
	CHECK(rereading->Left == doctest::Approx(answered->Left));
	CHECK(rereading->Right == doctest::Approx(answered->Right));

	const double pastTheWait = 0.25;
	screen.WaitSeconds(pastTheWait);

	const ImGuiWindow* body = FindTreeBody();
	REQUIRE(body != nullptr);
	const std::optional<HorizontalExtent> loader = messageExtent();
	REQUIRE(loader.has_value());
	CHECK(loader->Right > answered->Right);
	CHECK(loader->Right <= body->Pos.x + body->Size.x - Graphite::ListRowEndInset());
}

TEST_CASE("A filter matching nothing while the folder is read again shows a loader until the read lands")
{
	TempDirectory folder;
	folder.MakeFile("README.md");

	DeferredJobs jobs;
	ExplorerScreen screen{folder.GetPath(), jobs.Submitter()};
	screen.Click(kFilterBox);
	jobs.RunAll();
	screen.Next();

	screen.Events.Publish(Graphite::WorkspaceFilesChangedEvent{.Everything = true});
	screen.Type("Deep");
	screen.Next();
	REQUIRE(jobs.Count() == 1);
	static_cast<void>(Core::Redraw::TakeDeadline());

	screen.Next();

	CHECK(Core::Redraw::TakeDeadline().has_value());

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

TEST_CASE("Renaming a file tells the application which path moved where, so its tab can follow")
{
	TempDirectory folder;
	folder.MakeFile("a.cpp");

	ExplorerScreen screen{folder.GetPath()};

	std::vector<Graphite::PathMovedEvent> moved;
	screen.Events.Subscribe<Graphite::PathMovedEvent>(
		[&moved](const Graphite::PathMovedEvent& event) { moved.push_back(event); });

	screen.RightClickRow(kDoomedRow, kDoomedDepth);
	REQUIRE(screen.ChooseMenuItem("Rename"));
	screen.Next();
	screen.Next();
	screen.Type("renamed");
	screen.Press(ImGuiKey_Enter);
	screen.Next();

	REQUIRE(std::filesystem::is_regular_file(folder.GetPath() / "renamed.cpp"));
	REQUIRE(moved.size() == 1);
	CHECK(moved.front().From == folder.GetPath() / "a.cpp");
	CHECK(moved.front().To == folder.GetPath() / "renamed.cpp");
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

TEST_CASE("Move in the delete question closes the question at once and leaves the file on disk until the worker has moved it")
{
	TempDirectory folder;
	folder.MakeFile("doomed.txt");

	DeferredJobs jobs;
	ExplorerScreen screen{folder.GetPath(), jobs.Submitter()};

	screen.AskToDeleteRow(kDoomedRow, kDoomedDepth);
	REQUIRE(ImGuiFrames::IsPopupNamedOpen("Delete##fileTreeDelete"));

	REQUIRE(screen.ClickLastDialogButton("Move"));
	screen.Next();

	CHECK_FALSE(ImGuiFrames::IsPopupNamedOpen("Delete##fileTreeDelete"));
	CHECK_FALSE(screen.Modals.IsBusy());
	CHECK(std::filesystem::exists(folder.GetPath() / "doomed.txt"));
	CHECK(ChildNamed(screen.Workspace.GetTree().GetRoot(), "doomed.txt") != nullptr);
	REQUIRE(jobs.Count() == 1);

	jobs.RunAll();
	screen.Next();

	CHECK_FALSE(std::filesystem::exists(folder.GetPath() / "doomed.txt"));
	CHECK(ChildNamed(screen.Workspace.GetTree().GetRoot(), "doomed.txt") == nullptr);
}

TEST_CASE("While a file is moved to the Recycle Bin, its row offers no Rename, Delete or Duplicate")
{
	TempDirectory folder;
	folder.MakeFile("doomed.txt");

	DeferredJobs jobs;
	ExplorerScreen screen{folder.GetPath(), jobs.Submitter()};

	screen.AskToDeleteRow(kDoomedRow, kDoomedDepth);
	REQUIRE(screen.ClickLastDialogButton("Move"));
	screen.Next();
	REQUIRE(jobs.Count() == 1);

	const std::optional<bool> disabled{false};
	const std::optional<bool> enabled{true};

	screen.RightClickRow(kDoomedRow, kDoomedDepth);
	CHECK(screen.IsMenuItemEnabled("Rename") == disabled);
	CHECK(screen.IsMenuItemEnabled("Delete") == disabled);
	CHECK(screen.IsMenuItemEnabled("Duplicate") == disabled);
	CHECK(screen.IsMenuItemEnabled("Copy Path") == enabled);

	screen.Press(ImGuiKey_Escape);
	jobs.RunAll();
	screen.Next();
	screen.Next();

	CHECK(ChildNamed(screen.Workspace.GetTree().GetRoot(), "doomed.txt") == nullptr);
}

TEST_CASE("While a folder is duplicated, nothing new can be made inside it, and the rest of the folder stays free")
{
	TempDirectory folder;
	folder.MakeFile("busy/inside.txt");
	folder.MakeFile("free.txt");

	DeferredJobs jobs;
	ExplorerScreen screen{folder.GetPath(), jobs.Submitter()};

	constexpr int kBusyFolderRow = 1;
	constexpr int kFreeFileRow = 2;
	const std::optional<bool> disabled{false};
	const std::optional<bool> enabled{true};

	screen.RightClickRow(kBusyFolderRow, kDoomedDepth);
	REQUIRE(screen.ChooseMenuItem("Duplicate"));
	screen.Next();
	REQUIRE(jobs.Count() == 1);

	screen.RightClickRow(kBusyFolderRow, kDoomedDepth);
	CHECK(screen.IsMenuItemEnabled("New File") == disabled);
	CHECK(screen.IsMenuItemEnabled("New Folder") == disabled);
	CHECK(screen.IsMenuItemEnabled(kNewFileTemplateMenuItem) == disabled);
	screen.Press(ImGuiKey_Escape);

	screen.RightClickRow(kFreeFileRow, kDoomedDepth);
	CHECK(screen.IsMenuItemEnabled("Rename") == enabled);
	CHECK(screen.IsMenuItemEnabled("New File") == enabled);
	screen.Press(ImGuiKey_Escape);

	jobs.RunAll();
	screen.Next();
}

TEST_CASE("While a file inside a folder is moved to the Recycle Bin, the folder cannot be moved but still takes a new file")
{
	TempDirectory folder;
	folder.MakeFile("outer/doomed.txt");

	DeferredJobs jobs;
	ExplorerScreen screen{folder.GetPath(), jobs.Submitter()};

	constexpr int kOuterRow = 1;
	constexpr int kOuterDepth = 1;
	constexpr int kDoomedInsideRow = 2;
	constexpr int kDoomedInsideDepth = 2;
	const std::optional<bool> disabled{false};
	const std::optional<bool> enabled{true};

	screen.ClickRow(kOuterRow, kOuterDepth);
	screen.Next();
	screen.AskToDeleteRow(kDoomedInsideRow, kDoomedInsideDepth);
	REQUIRE(screen.ClickLastDialogButton("Move"));
	screen.Next();
	REQUIRE(jobs.Count() == 1);

	screen.RightClickRow(kOuterRow, kOuterDepth);
	CHECK(screen.IsMenuItemEnabled("Rename") == disabled);
	CHECK(screen.IsMenuItemEnabled("Delete") == disabled);
	CHECK(screen.IsMenuItemEnabled("Duplicate") == disabled);
	CHECK(screen.IsMenuItemEnabled("New File") == enabled);
	screen.Press(ImGuiKey_Escape);

	jobs.RunAll();
	screen.Next();
}

namespace {
	class Gate
	{
	public:
		void Open()
		{
			const std::lock_guard<std::mutex> lock(m_Mutex);
			m_Open = true;
			m_Opened.notify_all();
		}

		void WaitUntilOpen()
		{
			std::unique_lock<std::mutex> lock(m_Mutex);
			m_Opened.wait(lock, [this] { return m_Open; });
		}

	private:
		std::mutex m_Mutex;
		std::condition_variable m_Opened;
		bool m_Open = false;
	};

	struct CopyThenWait
	{
		Gate& MayFinish;

		Core::Result<std::filesystem::path> operator()(const Graphite::RunningFileOperation& operation,
													   const Core::CopiedFileCount& noteCopied) const
		{
			Core::Result<std::filesystem::path> copied = Graphite::RunFileOperation(operation, noteCopied);
			MayFinish.WaitUntilOpen();
			return copied;
		}
	};

	struct CopyOnItsOwnThread
	{
		Gate MayFinish;
		std::thread Worker;

		Core::JobSubmitter Submitter()
		{
			return [this](std::function<void()> job) { Worker = std::thread{std::move(job)}; };
		}

		void Finish()
		{
			MayFinish.Open();

			if (Worker.joinable())
			{
				Worker.join();
			}
		}

		~CopyOnItsOwnThread() { Finish(); }
	};

	bool WaitUntilExists(const std::filesystem::path& path)
	{
		constexpr std::chrono::seconds kTimeout{10};
		constexpr std::chrono::milliseconds kPollInterval{5};
		const std::chrono::steady_clock::time_point deadline = std::chrono::steady_clock::now() + kTimeout;

		while (!std::filesystem::exists(path) && std::chrono::steady_clock::now() < deadline)
		{
			std::this_thread::sleep_for(kPollInterval);
		}

		return std::filesystem::exists(path);
	}
}

TEST_CASE("The copy a duplicate is making offers no Rename, Delete or New File while the copy runs")
{
	TempDirectory folder;
	folder.MakeFile("box/inside.txt");

	CopyOnItsOwnThread copy;
	ExplorerScreen screen{folder.GetPath(), copy.Submitter(), CopyThenWait{.MayFinish = copy.MayFinish}};

	constexpr int kBoxRow = 1;
	constexpr int kCopyRow = 2;
	constexpr int kDepth = 1;
	const std::optional<bool> disabled{false};

	screen.RightClickRow(kBoxRow, kDepth);
	REQUIRE(screen.ChooseMenuItem("Duplicate"));
	REQUIRE(WaitUntilExists(folder.GetPath() / "box copy" / "inside.txt"));

	screen.Workspace.GetTree().Refresh(screen.Workspace.GetTree().GetRoot());
	screen.Next();
	REQUIRE(ChildNamed(screen.Workspace.GetTree().GetRoot(), "box copy") != nullptr);

	screen.RightClickRow(kCopyRow, kDepth);
	CHECK(screen.IsMenuItemEnabled("Rename") == disabled);
	CHECK(screen.IsMenuItemEnabled("Delete") == disabled);
	CHECK(screen.IsMenuItemEnabled("New File") == disabled);
	screen.Press(ImGuiKey_Escape);

	copy.Finish();
	screen.Next();
}

TEST_CASE("A path typed in the New File field that leads into a folder being copied is refused")
{
	TempDirectory folder;
	folder.MakeFile("box/inside.txt");

	DeferredJobs jobs;
	ExplorerScreen screen{folder.GetPath(), jobs.Submitter()};

	constexpr int kBoxRow = 1;
	constexpr int kDepth = 1;

	screen.RightClickRow(kBoxRow, kDepth);
	REQUIRE(screen.ChooseMenuItem("Duplicate"));
	screen.Next();
	REQUIRE(jobs.Count() == 1);

	screen.ChooseFromRootMenu("New File");
	screen.Type("box\\made.txt");
	screen.Press(ImGuiKey_Enter);
	screen.Next();

	CHECK_FALSE(std::filesystem::exists(folder.GetPath() / "box" / "made.txt"));

	jobs.RunAll();
	screen.Next();
}

TEST_CASE("New Folder passes over the name a running copy has reserved for its copy")
{
	TempDirectory folder;
	folder.MakeFile("new folder/inside.txt");

	DeferredJobs jobs;
	ExplorerScreen screen{folder.GetPath(), jobs.Submitter()};

	constexpr int kFolderRow = 1;
	constexpr int kDepth = 1;

	screen.RightClickRow(kFolderRow, kDepth);
	REQUIRE(screen.ChooseMenuItem("Duplicate"));
	screen.Next();
	REQUIRE(jobs.Count() == 1);

	screen.ChooseFromRootMenu("New Folder");

	CHECK(std::filesystem::is_directory(folder.GetPath() / "new folder copy 2"));
	CHECK_FALSE(std::filesystem::exists(folder.GetPath() / "new folder copy"));

	jobs.RunAll();
	screen.Next();

	CHECK(std::filesystem::exists(folder.GetPath() / "new folder copy" / "inside.txt"));
}

TEST_CASE("Refresh chosen from the folder's menu asks again whether each entry is ignored")
{
	TempDirectory folder;
	REQUIRE(Core::Repository::Init(folder.GetPath()));
	folder.MakeFile(".gitignore", "*.log\n");
	folder.MakeFile("build.log", "x");
	folder.MakeFile("notes.txt", "x");

	ExplorerScreen screen{folder.GetPath()};
	const Core::FileNode* notes = ChildNamed(screen.Workspace.GetTree().GetRoot(), "notes.txt");
	REQUIRE(notes != nullptr);
	REQUIRE_FALSE(notes->Ignored);

	folder.MakeFile(".gitignore", "notes.txt\n");
	screen.ChooseFromRootMenu("Refresh");

	notes = ChildNamed(screen.Workspace.GetTree().GetRoot(), "notes.txt");
	REQUIRE(notes != nullptr);
	CHECK(notes->Ignored);
}

TEST_CASE("Duplicate copies on a worker, and the copy appears in the tree once the copy is done")
{
	TempDirectory folder;
	folder.MakeFile("original.txt", "content");

	DeferredJobs jobs;
	ExplorerScreen screen{folder.GetPath(), jobs.Submitter()};

	screen.RightClickRow(kDoomedRow, kDoomedDepth);
	REQUIRE(screen.ChooseMenuItem("Duplicate"));
	screen.Next();

	REQUIRE(jobs.Count() == 1);
	CHECK_FALSE(std::filesystem::exists(folder.GetPath() / "original copy.txt"));

	jobs.RunAll();
	screen.Next();

	CHECK(std::filesystem::exists(folder.GetPath() / "original copy.txt"));
	CHECK(ChildNamed(screen.Workspace.GetTree().GetRoot(), "original copy.txt") != nullptr);
}
