#include <doctest/doctest.h>

#include "Git/Changes/ChangesPanel.h"
#include "Git/Changes/ChangesShared.h"
#include "Git/Changes/ChangesView.h"
#include "Git/Diff/DiffWindows.h"
#include "Git/GitModel.h"
#include "Git/Worktrees/WorktreeChanges.h"
#include "Git/Worktrees/WriteExpectation.h"
#include "Theme/ActiveScheme.h"
#include "Theme/Palettes.h"
#include "UI/Dialogs/ModalGate.h"
#include "Workspace/Workspace.h"

#include "Git/FakeEditorBuffers.h"
#include "DrawnColours.h"
#include "DrawnText.h"
#include "GitFixtures.h"
#include "HoveredItems.h"
#include "ImGuiFrames.h"

#include "Core/Events/EventBus.h"
#include "Core/Git/Head.h"
#include "Core/Git/Ref.h"

#include <IconsLucide.h>
#include <imgui.h>
#include <imgui_internal.h>

#include <cstddef>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {
	constexpr ImVec2 kHostSize(480.0f, 600.0f);
	constexpr float kIntoTheRow = 40.0f;
	constexpr int kOnlyEntry = 0;

	class OneModifiedFile final : public EmptyGitPort
	{
	public:
		Core::Result<Core::WorkingTreeStatus> QueryStatus() override
		{
			Core::FileStatus file;
			file.Path = "a.txt";
			file.WorkTree = Core::StatusFlags::Modified;
			return WorkingTreeStatusOf({file});
		}
	};

	// The host's window, at the same place and size every frame. Each test names its own, because hover
	// animations are kept by id across tests.
	std::function<void()> InHost(const char* host, std::function<void()> body, ImVec2 place = ImVec2(0.0f, 0.0f),
								 ImVec2 size = kHostSize)
	{
		return [host, body = std::move(body), place, size] {
			ImGui::SetNextWindowPos(place);
			ImGui::SetNextWindowSize(size);
			ImGui::Begin(host, nullptr, ImGuiWindowFlags_NoSavedSettings);
			body();
			ImGui::End();
		};
	}

	// What both hosts of the Changes view are made from, with a status of one modified file already landed.
	struct ChangesHosts
	{
		ChangesHosts()
		{
			Model.SetRoot("C:/repo");
			Model.RequestStatus();
			Model.OnUpdate(Core::Timestep{0.0f});
		}

		ImGuiFrames Frames;
		OneModifiedFile Port;
		Graphite::GitModel Model{Port, RunJobsInline()};
		Graphite::ModalGate Modals;
		Core::EventBus Events;
		FakeEditorBuffers Buffers;
		Graphite::ActiveScheme Scheme;
		Graphite::ChangesShared Shared;
		Graphite::DiffWindows Diffs;
	};

	const ImGuiWindow* ListWindow(std::string_view host)
	{
		constexpr std::string_view kListName = "changes.lists";

		for (const ImGuiWindow* window : ImGui::GetCurrentContext()->Windows)
		{
			const bool isTheList = (window->Flags & ImGuiWindowFlags_ChildWindow) != 0 && window->ParentWindow != nullptr &&
								   std::string_view{window->ParentWindow->Name} == host &&
								   std::string_view{window->Name}.find(kListName) != std::string_view::npos;

			if (isTheList)
			{
				return window;
			}
		}

		return nullptr;
	}

	// The ids the only file of the CHANGES section is drawn under, read off the list's window as ImGui hashes them.
	ImGuiID FileRowScope(const ImGuiWindow& list)
	{
		const ImGuiID section = ImHashStr("CHANGES", 0, list.ID);
		return ImHashData(&kOnlyEntry, sizeof(kOnlyEntry), section);
	}

	ImGuiID FileRowId(const ImGuiWindow& list)
	{
		return ImHashStr("##row", 0, FileRowScope(list));
	}

	// ImGui keeps no item's position past its frame, so the pointer steps down the list, and the middle of the
	// stretch where the file row's selectable is under it is the row's. Nothing when it never is.
	std::optional<ImVec2> PointOnFileRow(ImGuiFrames& frames, std::string_view host, const std::function<void()>& draw)
	{
		frames.Next(draw);

		const ImGuiWindow* list = ListWindow(host);

		if (list == nullptr)
		{
			return std::nullopt;
		}

		const ImGuiID rowId = FileRowId(*list);
		const float x = list->Pos.x + kIntoTheRow;

		for (const HoveredStretch& stretch : HoveredDown(frames, x, list->Pos.y, list->Pos.y + list->Size.y, draw))
		{
			if (stretch.Id == rowId)
			{
				return ImVec2(x, stretch.Middle());
			}
		}

		return std::nullopt;
	}

	bool ClickFileRow(ImGuiFrames& frames, std::string_view host, const std::function<void()>& draw)
	{
		const std::optional<ImVec2> onTheRow = PointOnFileRow(frames, host, draw);

		if (!onTheRow.has_value())
		{
			return false;
		}

		frames.Click(*onTheRow, draw);
		return true;
	}

	// The row shows its buttons only while the pointer is over it, so they are looked for across the row.
	bool ClickFileRowButton(ImGuiFrames& frames, std::string_view host, const std::function<void()>& draw,
							const char* button)
	{
		const std::optional<ImVec2> onTheRow = PointOnFileRow(frames, host, draw);
		const ImGuiWindow* list = ListWindow(host);

		if (!onTheRow.has_value() || list == nullptr)
		{
			return false;
		}

		const ImGuiID buttonId = ImHashStr(button, 0, FileRowScope(*list));
		const float listLeft = list->Pos.x;
		const float listRight = list->Pos.x + list->Size.x;

		for (const HoveredStretch& stretch : HoveredAcross(frames, onTheRow->y, listLeft, listRight, draw))
		{
			if (stretch.Id == buttonId)
			{
				frames.Click(ImVec2(stretch.Middle(), onTheRow->y), draw);
				return true;
			}
		}

		return false;
	}

	bool ClickFileRowDiscard(ImGuiFrames& frames, std::string_view host, const std::function<void()>& draw)
	{
		return ClickFileRowButton(frames, host, draw, "##discard");
	}

	// The commit message box drawn last frame in the host; null when none was.
	const ImGuiWindow* MessageBoxWindow(std::string_view host)
	{
		const ImGuiContext& context = *ImGui::GetCurrentContext();

		for (const ImGuiWindow* window : context.Windows)
		{
			const bool isTheBox = window->ParentWindow != nullptr && std::string_view{window->ParentWindow->Name} == host &&
								  std::string_view{window->Name}.find("commitMessage") != std::string_view::npos &&
								  window->LastFrameActive == context.FrameCount;

			if (isTheBox)
			{
				return window;
			}
		}

		return nullptr;
	}

	// The middle of the line of buttons under the message box: Commit, then Stash, then Amend.
	std::optional<float> CommitRowMiddle(std::string_view host)
	{
		const ImGuiWindow* box = MessageBoxWindow(host);

		if (box == nullptr)
		{
			return std::nullopt;
		}

		return box->Pos.y + box->Size.y + ImGui::GetStyle().ItemSpacing.y + ImGui::GetFrameHeight() * 0.5f;
	}

	// The items under the pointer moved along the line of the commit button.
	std::vector<HoveredStretch> AlongCommitRow(ImGuiFrames& frames, std::string_view host, const std::function<void()>& draw)
	{
		frames.Next(draw);

		const std::optional<float> middle = CommitRowMiddle(host);
		const ImGuiWindow* window = ImGui::FindWindowByName(std::string{host}.c_str());

		if (!middle.has_value() || window == nullptr)
		{
			return {};
		}

		return HoveredAcross(frames, *middle, window->Pos.x, window->Pos.x + window->Size.x, draw);
	}

	void ClickCommit(ImGuiFrames& frames, std::string_view host, const std::function<void()>& draw)
	{
		frames.Next(draw);

		const std::optional<float> middle = CommitRowMiddle(host);
		const ImGuiWindow* box = MessageBoxWindow(host);
		REQUIRE(middle.has_value());
		REQUIRE(box != nullptr);

		// The commit button starts the line, under the box's left edge.
		constexpr float kIntoTheButton = 20.0f;
		frames.Click(ImVec2(box->Pos.x + kIntoTheButton, *middle), draw);
	}

	// The text the host drew last frame, line by line and without spaces, in the face it was drawn in.
	std::vector<DrawnLine> HostText(std::string_view host, const ImFontBaked& font)
	{
		const ImGuiWindow* window = ImGui::FindWindowByName(std::string{host}.c_str());
		REQUIRE(window != nullptr);
		return DrawnTextLines(*window->DrawList, font);
	}

	bool DrawsLine(std::string_view host, const ImFontBaked& font, std::string_view text)
	{
		for (const DrawnLine& line : HostText(host, font))
		{
			if (line.Text.find(text) != std::string::npos)
			{
				return true;
			}
		}

		return false;
	}

	constexpr const char* kWorktreeBranch = "worktree-eager-scribbling-frog";
	constexpr const char* kWorktreeFolder = "eager-scribbling-frog";

	// Another worktree's repository: HEAD on its branch, one file staged and one modified, every write counted.
	class WorktreeLikePort final : public EmptyGitPort
	{
	public:
		Core::Result<std::filesystem::path> GetWorkingDirectory() override
		{
			return std::filesystem::path{"D:/work/eager-scribbling-frog"};
		}

		Core::Result<Core::WorkingTreeStatus> QueryStatus() override
		{
			if (StatusFailure.has_value())
			{
				return *StatusFailure;
			}

			Core::FileStatus staged;
			staged.Path = "staged.txt";
			staged.Index = Core::StatusFlags::Modified;

			Core::FileStatus modified;
			modified.Path = "a.txt";
			modified.WorkTree = Core::StatusFlags::Modified;

			// The modified file first, so the rows found by FileRowScope are its.
			return WorkingTreeStatusOf({modified, staged});
		}

		Core::Result<Core::HeadState> QueryHead() override { return Head; }

		Core::Result<void> Stage(const Paths& paths) override
		{
			++StageCalls;
			return EmptyGitPort::Stage(paths);
		}

		Core::Result<Core::Oid> Commit(const Core::CommitRequest& request) override
		{
			++CommitCalls;
			return EmptyGitPort::Commit(request);
		}

		Core::HeadState Head = [] {
			Core::HeadState head;
			head.BranchName = kWorktreeBranch;
			head.Commit = OidWithFirstByte(3);
			return head;
		}();

		int StageCalls = 0;
		int CommitCalls = 0;

		// Set, the status cannot be read: NotFound for a folder that is no longer a checkout.
		std::optional<Core::Error> StatusFailure;
	};

	// The model of that worktree, its status and HEAD already landed.
	struct WorktreeHost
	{
		WorktreeHost()
		{
			Model.SetRoot("D:/work/eager-scribbling-frog");
			Model.RequestStatus();
			Model.RequestHead();
			Model.OnUpdate(Core::Timestep{0.0f});
		}

		ImGuiFrames Frames;
		WorktreeLikePort Port;
		Graphite::GitModel Model{Port, RunJobsInline()};
		Graphite::ModalGate Modals;
		Core::EventBus Events;
		FakeEditorBuffers Buffers;
		Graphite::ActiveScheme Scheme;
		Graphite::ChangesShared Shared;
		Graphite::ChangesView View{Model, Modals, Buffers, Scheme, Events, Shared, "worktree"};

		// Each write's expectation, by whether it was asked for a commit.
		std::vector<bool> Asked;

		// What the user saw of the worktree, but for its branch, which another process has since switched.
		Graphite::ChangesViewOptions SwitchedAway()
		{
			Graphite::ChangesViewOptions options;
			options.OffersDiscard = false;
			options.OffersStash = false;
			options.Expect = [this](bool forCommit) {
				Asked.push_back(forCommit);

				Graphite::WriteExpectation expected;
				expected.BranchName = "other-branch";
				expected.Head = Port.Head.Commit;
				expected.Staged = std::vector<std::filesystem::path>{"staged.txt"};
				expected.Folder = kWorktreeFolder;
				return std::optional<Graphite::WriteExpectation>{expected};
			};
			return options;
		}
	};

	constexpr ImVec2 kWideHostSize(960.0f, 600.0f);
}

TEST_CASE("Picking a file hands its diff to the host")
{
	ChangesHosts hosts;
	Graphite::ChangesView view{hosts.Model, hosts.Modals, hosts.Buffers,
							   hosts.Scheme, hosts.Events, hosts.Shared, "window"};

	std::vector<Graphite::OpenDiff> chosen;
	view.OnFileChosen = [&chosen](const Graphite::OpenDiff& diff) { chosen.push_back(diff); };

	constexpr const char* kHost = "Picking host";
	const std::function<void()> draw = InHost(kHost, [&view] { view.Draw(); });

	REQUIRE(ClickFileRow(hosts.Frames, kHost, draw));

	REQUIRE(chosen.size() == 1);
	CHECK(chosen.front().Path == std::filesystem::path("a.txt"));
	CHECK(chosen.front().Target == Core::DiffTarget::WorkTreeToIndex);
	CHECK_FALSE(chosen.front().Commit.has_value());
}

TEST_CASE("Neither host takes a landed commit: that is left to the write consequences")
{
	ChangesHosts hosts;
	Graphite::ChangesView view{hosts.Model, hosts.Modals, hosts.Buffers,
							   hosts.Scheme, hosts.Events, hosts.Shared, "window"};
	Graphite::ChangesPanel panel{hosts.Model, hosts.Modals, hosts.Buffers,
								 hosts.Scheme, hosts.Diffs, hosts.Events, hosts.Shared};

	hosts.Shared.Message = "fix";

	hosts.Model.RequestCommit({.Message = hosts.Shared.Message});
	hosts.Model.OnUpdate(Core::Timestep{0.0f});

	hosts.Frames.Next(InHost("Own view host", [&view] { view.Draw(); }));
	panel.OnUpdate(Core::Timestep{0.0f});

	CHECK(hosts.Shared.Message == "fix");
	CHECK(hosts.Model.TakeCommitted().has_value());
}

TEST_CASE("The side panel opens a diff tab for the picked file")
{
	ChangesHosts hosts;
	Graphite::ChangesPanel panel{hosts.Model, hosts.Modals, hosts.Buffers,
								 hosts.Scheme, hosts.Diffs, hosts.Events, hosts.Shared};

	constexpr const char* kHost = "Side panel host";
	const std::function<void()> draw = InHost(kHost, [&panel] { panel.OnImGuiRender(); });

	REQUIRE(ClickFileRow(hosts.Frames, kHost, draw));

	CHECK(hosts.Diffs.GetCount() == 1);
	CHECK(hosts.Diffs.IsOpen(Graphite::OpenDiff{.Commit = std::nullopt,
												.Target = Core::DiffTarget::WorkTreeToIndex,
												.Path = "a.txt"}));
}

TEST_CASE("A side panel left undrawn hands back the modal gate its discard question held")
{
	ChangesHosts hosts;
	Graphite::ChangesPanel panel{hosts.Model, hosts.Modals, hosts.Buffers,
								 hosts.Scheme, hosts.Diffs, hosts.Events, hosts.Shared};

	constexpr const char* kHost = "Undrawn panel host";
	const std::function<void()> draw = InHost(kHost, [&panel] {
		panel.OnImGuiRender();
		panel.SetDrawnOnLatestFrame(true);
	});

	REQUIRE(ClickFileRowDiscard(hosts.Frames, kHost, draw));
	hosts.Frames.Next(draw);

	REQUIRE(hosts.Modals.IsBusy());

	panel.OnUpdate(Core::Timestep{0.0f});
	CHECK(hosts.Modals.IsBusy());

	panel.SetDrawnOnLatestFrame(false);
	panel.OnUpdate(Core::Timestep{0.0f});
	CHECK_FALSE(hosts.Modals.IsBusy());
}

TEST_CASE("Two Changes views ask their questions under their own identities")
{
	ChangesHosts hosts;
	Graphite::ChangesView panelView{hosts.Model, hosts.Modals, hosts.Buffers,
									hosts.Scheme, hosts.Events, hosts.Shared, "panel"};
	Graphite::ChangesView windowView{hosts.Model, hosts.Modals, hosts.Buffers,
									 hosts.Scheme, hosts.Events, hosts.Shared, "window"};

	constexpr const char* kPanelHost = "Two views panel host";
	constexpr const char* kWindowHost = "Two views window host";
	constexpr ImVec2 kBesideThePanel(kHostSize.x + 20.0f, 0.0f);

	const std::function<void()> drawPanel = InHost(kPanelHost, [&panelView] { panelView.Draw(); });
	const std::function<void()> drawWindow = InHost(kWindowHost, [&windowView] { windowView.Draw(); }, kBesideThePanel);

	const std::function<void()> drawBoth = [&drawPanel, &drawWindow] {
		drawPanel();
		drawWindow();
	};

	REQUIRE(ClickFileRowDiscard(hosts.Frames, kPanelHost, drawBoth));
	hosts.Frames.Next(drawBoth);

	REQUIRE(hosts.Modals.IsBusy());

	// The other view, drawn on the same frames, must not take the panel's question for one of its own and hand
	// back the gate it holds.
	constexpr int kFramesWithBothDrawn = 3;

	for (int frame = 0; frame < kFramesWithBothDrawn; ++frame)
	{
		hosts.Frames.Next(drawBoth);
	}

	CHECK(hosts.Modals.IsBusy());

	panelView.WithdrawQuestions();
	CHECK_FALSE(hosts.Modals.IsBusy());
}

TEST_CASE("Changes view without Discard offers no discard on a file's row or in its menu")
{
	WorktreeHost host;
	host.View.SetOptions(host.SwitchedAway());

	constexpr const char* kHost = "No discard host";
	const std::function<void()> draw = InHost(kHost, [&host] { host.View.Draw(); });

	CHECK_FALSE(ClickFileRowDiscard(host.Frames, kHost, draw));
	CHECK_FALSE(host.Modals.IsBusy());

	const std::optional<ImVec2> onTheRow = PointOnFileRow(host.Frames, kHost, draw);
	REQUIRE(onTheRow.has_value());
	host.Frames.Click(*onTheRow, draw, ImGuiMouseButton_Right);

	CHECK_FALSE(host.Frames.ClickMenuItem("Discard Changes...", draw));

	// The menu is open and read: what it does offer is there.
	CHECK(host.Frames.ClickMenuItem("Copy File Name", draw));
}

TEST_CASE("A file row's hover Open icon opens the file of the view's own working directory, and a plain click only selects")
{
	WorktreeHost host;
	std::vector<std::filesystem::path> opened;
	host.Events.Subscribe<Graphite::OpenFileRequested>(
		[&opened](const Graphite::OpenFileRequested& request) { opened.push_back(request.Path); });

	constexpr const char* kHost = "Open icon host";
	const std::function<void()> draw = InHost(kHost, [&host] { host.View.Draw(); });

	REQUIRE(ClickFileRow(host.Frames, kHost, draw));
	CHECK(opened.empty());

	REQUIRE(ClickFileRowButton(host.Frames, kHost, draw, "##open"));
	REQUIRE(opened.size() == 1);
	CHECK(opened.front() == std::filesystem::path{"D:/work/eager-scribbling-frog"} / "a.txt");
}

TEST_CASE("Changes view offers Discard on a file's row by default")
{
	WorktreeHost host;

	constexpr const char* kHost = "Default discard host";
	const std::function<void()> draw = InHost(kHost, [&host] { host.View.Draw(); });

	CHECK(ClickFileRowDiscard(host.Frames, kHost, draw));
}

TEST_CASE("Changes view without Stash draws no Stash button beside Commit")
{
	for (const bool offersStash : {true, false})
	{
		CAPTURE(offersStash);

		WorktreeHost host;
		Graphite::ChangesViewOptions options = host.SwitchedAway();
		options.OffersStash = offersStash;
		host.View.SetOptions(options);

		const char* const hostName = offersStash ? "Stash offered host" : "No stash host";
		const std::function<void()> draw = InHost(hostName, [&host] { host.View.Draw(); });

		host.Frames.Next(draw);
		const ImGuiWindow* window = ImGui::FindWindowByName(hostName);
		REQUIRE(window != nullptr);
		const ImGuiID stashId = ImHashStr(ICON_LC_ARCHIVE "##stash", 0, window->ID);

		bool stashFound = false;

		for (const HoveredStretch& stretch : AlongCommitRow(host.Frames, hostName, draw))
		{
			stashFound = stashFound || stretch.Id == stashId;
		}

		CHECK(stashFound == offersStash);
	}
}

TEST_CASE("Changes view names the worktree's branch and folder on its commit button, and a detached HEAD in the warning colour")
{
	WorktreeHost host;
	host.Shared.Message = "fix";

	ImFontBaked* font = nullptr;
	Graphite::ChangesViewOptions options = host.SwitchedAway();
	options.CommitLabel = [&host] { return Graphite::WorktreeCommitLabel(host.Model.GetHead().get(), kWorktreeFolder); };
	host.View.SetOptions(options);

	constexpr const char* kHost = "Commit label host";
	const std::function<void()> draw = InHost(kHost, [&host, &font] {
		font = ImGui::GetFontBaked();
		host.View.Draw();
	}, ImVec2(0.0f, 0.0f), kWideHostSize);

	host.Frames.Next(draw);
	REQUIRE(font != nullptr);
	CHECK(DrawsLine(kHost, *font, "Committoworktree-eager-scribbling-frogineager-scribbling-frog"));

	const ImU32 warning = Graphite::ReadEditorPalette(host.Scheme.Get()).DiagnosticWarning;
	const ImGuiWindow* window = ImGui::FindWindowByName(kHost);
	REQUIRE(window != nullptr);
	CHECK_FALSE(DrawsInColour(*window->DrawList, warning));

	host.Port.Head.Kind = Core::HeadKind::Detached;
	host.Port.Head.BranchName.clear();
	host.Model.RequestHead();
	host.Model.OnUpdate(Core::Timestep{0.0f});

	options.CommitWarns = true;
	host.View.SetOptions(options);
	host.Frames.Next(draw);

	CHECK(DrawsLine(kHost, *font, "CommitondetachedHEAD"));
	CHECK(DrawsInColour(*window->DrawList, warning));
}

TEST_CASE("Changes view's commit carries the expectation asked for a commit, and is refused when the worktree no longer holds it")
{
	WorktreeHost host;
	host.Shared.Message = "fix";
	host.View.SetOptions(host.SwitchedAway());

	constexpr const char* kHost = "Commit expectation host";
	const std::function<void()> draw = InHost(kHost, [&host] { host.View.Draw(); }, ImVec2(0.0f, 0.0f), kWideHostSize);

	ClickCommit(host.Frames, kHost, draw);
	host.Model.OnUpdate(Core::Timestep{0.0f});

	CHECK(host.Asked == std::vector<bool>{true});
	CHECK(host.Port.CommitCalls == 0);

	const std::optional<Graphite::GitModel::WriteFailure> refused = host.Model.TakeWriteError();
	REQUIRE(refused.has_value());
	CHECK(refused->Error.Message.starts_with("Not committed:"));
}

TEST_CASE("Changes view's stage carries the expectation asked for a write that is no commit")
{
	WorktreeHost host;
	host.View.SetOptions(host.SwitchedAway());

	constexpr const char* kHost = "Stage expectation host";
	const std::function<void()> draw = InHost(kHost, [&host] { host.View.Draw(); });

	REQUIRE(ClickFileRowButton(host.Frames, kHost, draw, "##stage"));
	host.Model.OnUpdate(Core::Timestep{0.0f});

	CHECK(host.Asked == std::vector<bool>{false});
	CHECK(host.Port.StageCalls == 0);

	const std::optional<Graphite::GitModel::WriteFailure> refused = host.Model.TakeWriteError();
	REQUIRE(refused.has_value());
	CHECK(refused->Error.Message.starts_with("Not staged:"));
}

TEST_CASE("Changes view draws the line naming where its changes are above the lists")
{
	WorktreeHost host;
	Graphite::ChangesViewOptions options = host.SwitchedAway();
	options.WhereLine = "Worktree \xC2\xB7 D:/work/eager-scribbling-frog";
	host.View.SetOptions(options);

	ImFontBaked* font = nullptr;
	constexpr const char* kHost = "Where line host";
	const std::function<void()> draw = InHost(kHost, [&host, &font] {
		font = ImGui::GetFontBaked();
		host.View.Draw();
	});

	host.Frames.Next(draw);
	REQUIRE(font != nullptr);
	CHECK(DrawsLine(kHost, *font, "WorktreeD:/work/eager-scribbling-frog"));
}

TEST_CASE("Changes view edits the draft it is given, and leaves the one it was made with")
{
	WorktreeHost host;
	Graphite::ChangesShared worktreeDraft;
	host.View.SetShared(worktreeDraft);

	constexpr const char* kHost = "Draft swap host";
	const std::function<void()> draw = InHost(kHost, [&host] { host.View.Draw(); });

	host.Frames.Next(draw);
	const ImGuiWindow* box = MessageBoxWindow(kHost);
	REQUIRE(box != nullptr);

	host.Frames.Click(ImVec2(box->Pos.x + box->Size.x * 0.5f, box->Pos.y + box->Size.y * 0.5f), draw);
	ImGui::GetIO().AddInputCharactersUTF8("wip");
	host.Frames.Next(draw);
	host.Frames.Next(draw);

	CHECK(worktreeDraft.Message == "wip");
	CHECK(host.Shared.Message.empty());
}

TEST_CASE("Changes view asks before amending a pushed commit when its branch is answered by the host")
{
	WorktreeHost host;
	host.Shared.Message = "fix";
	host.Shared.Amend = true;

	Graphite::ChangesViewOptions options = host.SwitchedAway();
	options.CheckedOutBranch = [] {
		Core::Branch branch;
		branch.Name = kWorktreeBranch;
		branch.FullName = std::string{"refs/heads/"} + kWorktreeBranch;
		branch.IsHead = true;
		branch.Upstream = std::string{"origin/"} + kWorktreeBranch;
		branch.DistanceKnown = true;
		branch.Ahead = 0;
		return std::optional<Core::Branch>{branch};
	};
	host.View.SetOptions(options);

	constexpr const char* kHost = "Amend pushed host";
	const std::function<void()> draw = InHost(kHost, [&host] { host.View.Draw(); }, ImVec2(0.0f, 0.0f), kWideHostSize);

	ClickCommit(host.Frames, kHost, draw);
	host.Frames.Next(draw);
	host.Model.OnUpdate(Core::Timestep{0.0f});

	CHECK(host.Modals.IsBusy());
	CHECK(host.Asked.empty());
	CHECK(host.Port.CommitCalls == 0);

	host.View.WithdrawQuestions();
}

TEST_CASE("Changes view without Initialize says the folder is missing where a repository is not found")
{
	for (const bool offersInit : {true, false})
	{
		CAPTURE(offersInit);

		WorktreeHost host;
		host.Port.StatusFailure = Core::Error{Core::ErrorCode::NotFound, "no repository at D:/work/eager-scribbling-frog"};
		host.Model.Clear();
		host.Model.SetRoot("D:/work/eager-scribbling-frog");
		host.Model.RequestStatus();
		host.Model.OnUpdate(Core::Timestep{0.0f});
		REQUIRE(host.Model.GetStatus() == nullptr);

		Graphite::ChangesViewOptions options = host.SwitchedAway();
		options.OffersInit = offersInit;
		host.View.SetOptions(options);

		ImFontBaked* font = nullptr;
		const char* const hostName = offersInit ? "Init offered host" : "No init host";
		const std::function<void()> draw = InHost(hostName, [&host, &font] {
			font = ImGui::GetFontBaked();
			host.View.Draw();
		}, ImVec2(0.0f, 0.0f), kWideHostSize);

		host.Frames.Next(draw);
		REQUIRE(font != nullptr);

		CHECK(DrawsLine(hostName, *font, "InitializeRepository") == offersInit);
		CHECK(DrawsLine(hostName, *font, "Foldermissing") == !offersInit);
	}
}

TEST_CASE("Changes view's empty message box names the worktree's target, and the open folder's says Message")
{
	WorktreeHost host;

	ImFontBaked* font = nullptr;
	constexpr const char* kHost = "Message hint host";
	const std::function<void()> draw = InHost(kHost, [&host, &font] {
		font = ImGui::GetFontBaked();
		host.View.Draw();
	}, ImVec2(0.0f, 0.0f), kWideHostSize);

	host.Frames.Next(draw);
	REQUIRE(font != nullptr);
	CHECK(DrawsLine(kHost, *font, "Message"));
	CHECK_FALSE(DrawsLine(kHost, *font, "Messagefor"));

	Graphite::ChangesViewOptions options = host.SwitchedAway();
	options.MessageHint = [&host] { return Graphite::WorktreeMessageHint(host.Model.GetHead().get(), kWorktreeFolder); };
	host.View.SetOptions(options);
	host.Frames.Next(draw);

	CHECK(DrawsLine(kHost, *font, "Messageforworktree-eager-scribbling-frogineager-scribbling-frog"));

	host.Port.Head.Kind = Core::HeadKind::Detached;
	host.Port.Head.BranchName.clear();
	host.Model.RequestHead();
	host.Model.OnUpdate(Core::Timestep{0.0f});
	host.Frames.Next(draw);

	CHECK(DrawsLine(kHost, *font, "MessagefordetachedHEADineager-scribbling-frog"));
}

TEST_CASE("Changes view's message hint too long for a narrow box stays inside the box")
{
	WorktreeHost host;

	ImFontBaked* font = nullptr;
	constexpr const char* kHost = "Narrow message hint host";
	constexpr ImVec2 kNarrowHostSize(260.0f, 600.0f);
	const std::function<void()> draw = InHost(kHost, [&host, &font] {
		font = ImGui::GetFontBaked();
		host.View.Draw();
	}, ImVec2(0.0f, 0.0f), kNarrowHostSize);

	Graphite::ChangesViewOptions options = host.SwitchedAway();
	options.MessageHint = [&host] { return Graphite::WorktreeMessageHint(host.Model.GetHead().get(), kWorktreeFolder); };
	host.View.SetOptions(options);
	host.Frames.Next(draw);
	host.Frames.Next(draw);
	REQUIRE(font != nullptr);

	const ImGuiWindow* box = MessageBoxWindow(kHost);
	REQUIRE(box != nullptr);
	const float boxRight = box->Pos.x + box->Size.x;
	const float boxBottom = box->Pos.y + box->Size.y;

	std::string hintRead;
	float rightmost = 0.0f;

	for (const DrawnLine& line : HostText(kHost, *font))
	{
		if (line.Top < box->Pos.y || line.Top >= boxBottom)
		{
			continue;
		}

		hintRead += line.Text;

		for (const float right : line.Rights)
		{
			rightmost = std::max(rightmost, right);
		}
	}

	CHECK(hintRead.find("Messagefor") == 0);
	CHECK(rightmost <= boxRight);
}
