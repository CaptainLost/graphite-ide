#include <doctest/doctest.h>

#include "DeferredJobs.h"
#include "DrawnColours.h"
#include "DrawnText.h"
#include "GitFixtures.h"
#include "HoveredItems.h"
#include "ImGuiFrames.h"
#include "TempAppSettings.h"
#include "WorktreeFixtures.h"
#include "Git/FakeEditorBuffers.h"
#include "Git/Lines/LineReadFixtures.h"

#include "Editor/EditorSettings.h"
#include "Git/Changes/ChangesShared.h"
#include "Git/Changes/ChangesView.h"
#include "Git/Content/ContentModel.h"
#include "Git/Content/ContentPort.h"
#include "Git/Debouncer.h"
#include "Git/Diff/DiffWindows.h"
#include "Git/GitView.h"
#include "Git/Lines/LinesModel.h"
#include "Git/Lines/LinesPort.h"
#include "Git/Network/NetworkModel.h"
#include "Git/RepositorySession.h"
#include "Git/Sidebar/SidebarView.h"
#include "Git/Window/WindowBar.h"
#include "Git/Worktrees/WorktreeChanges.h"
#include "Git/Worktrees/WorktreePort.h"
#include "Git/Worktrees/WorktreesModel.h"
#include "Git/WriteConsequences.h"
#include "Review/WorktreeWatch.h"
#include "Theme/ActiveScheme.h"
#include "Theme/GitPalette.h"
#include "UI/Dialogs/ModalGate.h"
#include "UI/Lists/ListRow.h"
#include "UI/TickBox.h"
#include "Workspace/Workspace.h"

#include "Core/Events/EventBus.h"
#include "Core/FileSystem/FileSystem.h"
#include "Core/Git/Lines/AncestryGraph.h"
#include "Core/Git/Stash.h"
#include "Core/Git/Tag.h"

#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>
#include <cfloat>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <functional>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace {

	using Graphite::Fixtures::LineReadBuilder;

	constexpr int kTopicCount = 30;
	constexpr ImVec2 kWindowSize(1200.0f, 700.0f);

	// The mockup's .when, the bar's last-fetch age, is 12 px of its 13.
	constexpr float kAgeMockupPixels = 12.0f;

	Core::HeadState HeadOnMain()
	{
		Core::HeadState head;
		head.Kind = Core::HeadKind::Branch;
		head.BranchName = "main";
		head.Commit = OidWithFirstByte(1);
		return head;
	}

	Core::HeadState DetachedHead()
	{
		Core::HeadState head;
		head.Kind = Core::HeadKind::Detached;
		head.Commit = OidWithFirstByte(1);
		return head;
	}

	// The read the window's sidebar is built from: main (checked out when HEAD is on it), then a feature branch and
	// enough topics to scroll past.
	Graphite::LineRead MakeRead(const Core::HeadState& head)
	{
		LineReadBuilder builder;
		builder.AddLocal("main", {.Head = head.Kind == Core::HeadKind::Branch, .AgeDays = 40});
		builder.AddLocal("feature", {.AgeDays = 2});

		for (int topic = 0; topic < kTopicCount; ++topic)
		{
			builder.AddLocal("topic-" + std::to_string(topic), {.AgeDays = 3 + topic});
		}

		// A read always holds a graph; this one holds none of the commits the histories show.
		Graphite::LineRead read = builder.Build();
		read.Graph = Core::CreateRef<const Core::AncestryGraph>();
		return read;
	}

	Core::Tag TagAt(const std::string& name, const Core::Oid& target)
	{
		Core::Tag tag;
		tag.Name = name;
		tag.FullName = "refs/tags/" + name;
		tag.Target = target;
		return tag;
	}

	// The repository as git answers it: the read's branches, the HEAD, the tags and the stashes given, no remotes. A
	// history is one commit: the stash asked for, or the tip of the reference it is asked for.
	class FixedHeadPort final : public EmptyGitPort
	{
	public:
		FixedHeadPort(std::vector<Core::Branch> branches, Core::HeadState head, std::vector<Core::Tag> tags)
			: Branches(std::move(branches))
			, Tags(std::move(tags))
			, m_Head(std::move(head))
		{
		}

		Core::Result<std::vector<Core::Branch>> QueryBranches() override { return Branches; }

		Core::Result<std::vector<Core::Tag>> QueryTags(Core::TagCache&) override { return Tags; }

		Core::Result<Core::HeadState> QueryHead() override { return m_Head; }

		Core::Result<std::vector<Core::StashEntry>> QueryStashes() override { return Stashes; }

		Core::Result<std::vector<Core::Remote>> QueryRemotes() override { return Remotes; }

		Core::Result<Core::LogPage> QueryLog(const Core::LogRequest& request) override
		{
			Core::LogPage page;
			page.Complete = true;

			const std::optional<Core::Oid> start = request.From.has_value() ? request.From : TipOf(request.Ref);

			if (const std::optional<Core::Oid> tip = request.Stash.has_value() ? request.Stash : start)
			{
				Core::CommitSummary commit;
				commit.Id = *tip;
				page.Commits.push_back(std::move(commit));
			}

			return page;
		}

		// What a fetch changes.
		std::vector<Core::Branch> Branches;
		std::vector<Core::Tag> Tags;

		std::vector<Core::StashEntry> Stashes;
		std::vector<Core::Remote> Remotes;

	private:
		std::optional<Core::Oid> TipOf(const std::optional<std::string>& ref) const
		{
			if (!ref.has_value())
			{
				return m_Head.Commit;
			}

			for (const Core::Branch& branch : Branches)
			{
				if (branch.FullName == *ref)
				{
					return branch.Tip;
				}
			}

			for (const Core::Tag& tag : Tags)
			{
				if (tag.FullName == *ref)
				{
					return tag.Target;
				}
			}

			return std::nullopt;
		}

		Core::HeadState m_Head;
	};

	class OneReadLinesPort final : public Graphite::LinesPort
	{
	public:
		explicit OneReadLinesPort(Graphite::LineRead read) : m_Read(std::move(read)) {}

		std::uint64_t RepositoryNow() override { return 1; }

		Core::Result<Graphite::LineRead> ReadLines(std::uint64_t, const Graphite::LineReadInput& input) override
		{
			Graphite::LineRead read = m_Read;
			read.Branches = input.Branches;
			read.Tags = input.Tags;
			return read;
		}

		Core::Result<Graphite::LineRead> RecomputeLines(const Graphite::LineRead& last, const Graphite::LineAnswerChange&,
														std::int64_t) override
		{
			return last;
		}

	private:
		Graphite::LineRead m_Read;
	};

	// No worktree but the ones a test lists, each clean and idle.
	class ListedWorktreesPort final : public Graphite::WorktreePort
	{
	public:
		Core::Result<std::vector<Core::WorktreeEntry>> QueryWorktrees() override
		{
			if (ListFailure.has_value())
			{
				return *ListFailure;
			}

			return Listed;
		}

		Core::Result<Graphite::WorktreeActivity> QueryActivity(const std::filesystem::path&) override
		{
			return Graphite::WorktreeActivity{};
		}

		Core::Result<Graphite::WorktreeCount> CountUncommitted(const std::filesystem::path&) override
		{
			return Graphite::WorktreeCount{};
		}

		std::optional<Core::Error> ListFailure; // set, the list cannot be read
		std::vector<Core::WorktreeEntry> Listed;
	};

	// Another worktree's repository, wherever it is pointed: HEAD on feature, one file staged and two modified.
	class WorktreeStatusPort final : public EmptyGitPort
	{
	public:
		Core::Result<Core::WorkingTreeStatus> QueryStatus() override
		{
			Core::FileStatus staged;
			staged.Path = "staged.txt";
			staged.Index = Core::StatusFlags::Modified;

			Core::FileStatus one;
			one.Path = "one.txt";
			one.WorkTree = Core::StatusFlags::Modified;

			Core::FileStatus two;
			two.Path = "two.txt";
			two.WorkTree = Core::StatusFlags::Modified;

			return WorkingTreeStatusOf({staged, one, two});
		}

		Core::Result<Core::HeadState> QueryHead() override
		{
			Core::HeadState head;
			head.BranchName = Branch;
			head.Commit = OidWithFirstByte(2);
			head.Operation = Operation;
			return head;
		}

		// What another process may change under the window.
		std::string Branch = "feature";
		Core::RepositoryOperation Operation = Core::RepositoryOperation::None;
	};

	// A watch that starts and never reports a change.
	class QuietWatch final : public Graphite::WorktreeWatch
	{
	public:
		bool TakeChanged() override { return false; }
	};

	class NoContentPort final : public Graphite::ContentPort
	{
	public:
		std::uint64_t RepositoryNow() override { return 1; }

		Core::Result<Core::ContentVerdict> CheckContent(std::uint64_t, const Core::Oid&, const Core::Oid&,
														Core::PatchIdCache&) override
		{
			return Unanswered();
		}

		Core::Result<Core::PatchTwinSearch> FindTwin(std::uint64_t, const Core::Oid&, const Core::Oid&, std::size_t,
													 Core::PatchIdCache&) override
		{
			return Unanswered();
		}

		Core::Result<std::vector<std::string>> PathsChangedBetween(std::uint64_t, const Core::Oid&, const Core::Oid&) override
		{
			return Unanswered();
		}

		Core::Result<bool> TreesDifferAt(std::uint64_t, const Core::Oid&, const Core::Oid&,
										 const std::vector<std::string>&) override
		{
			return Unanswered();
		}

	private:
		static Core::Error Unanswered() { return Core::Error{Core::ErrorCode::IoFailure, "not asked in this test"}; }
	};

	// Yours lists main, feature and the topics, each two lines high, under its one-line header.
	constexpr int kBranchCount = 2 + kTopicCount;

	// The slot of the branch row at `place` in Yours, 0 for its first row.
	float YoursRowTop(int place)
	{
		return Graphite::ListRowPitch() + static_cast<float>(place) * Graphite::SidebarTwoLineRowHeight();
	}

	// The Git window drawn alone, its models fed by the ports above and answered at once, the line reads unless they
	// are held. `read` stands for MakeRead's.
	struct GitWindow
	{
		explicit GitWindow(const char* windowName, Core::HeadState head = HeadOnMain(), std::vector<Core::Tag> tags = {},
						   std::optional<Graphite::LineRead> read = std::nullopt)
			: WindowName(windowName)
			, Head(std::move(head))
			, Read(read.has_value() ? std::move(*read) : MakeRead(Head))
			, Port{*Read.Branches, Head, std::move(tags)}
		{
			Model.SetRoot("C:/repo");
			Model.SubscribeTo(Events);
			Model.RequestStatus();
			Model.RequestBranches();
			Model.RequestHead();
			Model.RequestStashes();
		}

		void Step()
		{
			const Core::Timestep noTime{0.0f};

			Repository.Update(noTime);
			WorktreeChanges.OnUpdate(noTime);
			View.OnUpdate(noTime);

			Frames.Next([this] { DrawWindow(); });

			View.SetDrawnOnLatestFrame(true);
		}

		// The window's frame alone, with no model updated before it.
		void DrawWindow()
		{
			ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
			ImGui::SetNextWindowSize(kWindowSize);
			ImGui::Begin(WindowName, nullptr, ImGuiWindowFlags_NoSavedSettings);
			BarTop = ImGui::GetCursorScreenPos().y - ImGui::GetStyle().WindowPadding.y;
			Font = ImGui::GetFontBaked();
			AgeFont = ImGui::GetFont()->GetFontBaked(Graphite::WindowBarEm(kAgeMockupPixels));
			View.OnImGuiRender();
			ChipId = ImGui::GetID("##headChip");
			FetchId = ImGui::GetID("##networkFetch");
			PushId = ImGui::GetID("##networkPush");
			ImGui::End();
		}

		void Settle(int frames = 6)
		{
			for (int frame = 0; frame < frames; ++frame)
			{
				Step();
			}
		}

		// Moves the pointer along the bar's middle until the chip is under it; false when it never is.
		bool PointAtChip()
		{
			return PointAt(ChipId);
		}

		// Moves the pointer along the bar's middle until the item `id` is under it; false when it never is.
		bool PointAt(ImGuiID id)
		{
			constexpr float kStep = 4.0f;
			const float middle = BarTop + Graphite::WindowBarEm(36.0f) * 0.5f;

			for (float x = 0.0f; x < kWindowSize.x; x += kStep)
			{
				ImGui::GetIO().AddMousePosEvent(x, middle);
				Step();

				if (ImGui::GetCurrentContext()->HoveredId == id)
				{
					return true;
				}
			}

			return false;
		}

		// Reads the repository again, as after a change on disk, so a port set up after the window began is heard.
		void ListAgain()
		{
			Model.RequestBranches();
			Settle();
		}

		// One event a frame, as a person clicks where the pointer is.
		void Click()
		{
			ImGui::GetIO().AddMouseButtonEvent(ImGuiMouseButton_Left, true);
			Step();
			ImGui::GetIO().AddMouseButtonEvent(ImGuiMouseButton_Left, false);
			Step();
		}

		// Clicks at `at`, then draws the frame that answers the click: a click on a sidebar row is answered while its rows
		// are drawn, so what it changes is drawn in the frame after.
		void ClickAt(ImVec2 at)
		{
			ImGui::GetIO().AddMousePosEvent(at.x, at.y);
			Step();
			Click();
			Step();
		}

		// The sidebar's row whose middle is `down` the list's content, where it is on screen now.
		ImVec2 InSidebarRows(float down) const
		{
			const ImGuiWindow* rows = SidebarRows();
			return ImVec2(rows->Pos.x + 60.0f, rows->DC.CursorStartPos.y + down);
		}

		// The middle of the row the sidebar draws selected, as far down the list's content; empty when it draws none.
		std::optional<float> SelectedRowMiddle() const
		{
			const ImGuiWindow* rows = SidebarRows();

			if (rows == nullptr)
			{
				return std::nullopt;
			}

			const ImU32 selected = ImGui::GetColorU32(Graphite::CurrentListRowColours().Selected);
			float top = FLT_MAX;
			float bottom = -FLT_MAX;

			for (const ImDrawVert& vertex : rows->DrawList->VtxBuffer)
			{
				if (vertex.col == selected)
				{
					top = std::min(top, vertex.pos.y);
					bottom = std::max(bottom, vertex.pos.y);
				}
			}

			if (top > bottom)
			{
				return std::nullopt;
			}

			return (top + bottom) * 0.5f - rows->DC.CursorStartPos.y;
		}

		// The Changes view's lists, which the preview shows while Local changes is the row selected, were drawn last frame.
		bool DrawsLocalChanges() const
		{
			const ImGuiContext& context = *ImGui::GetCurrentContext();

			for (const ImGuiWindow* window : context.Windows)
			{
				if (std::strstr(window->Name, WindowName) != nullptr && std::strstr(window->Name, "changes.lists") != nullptr &&
					window->LastFrameActive == context.FrameCount)
				{
					return true;
				}
			}

			return false;
		}

		// Whether a window named `child` inside this one, or one inside it, drew in `colour` last frame.
		bool DrawsInColourIn(const char* child, ImU32 colour) const
		{
			const ImGuiContext& context = *ImGui::GetCurrentContext();

			for (const ImGuiWindow* window : context.Windows)
			{
				if (std::strstr(window->Name, WindowName) != nullptr && std::strstr(window->Name, child) != nullptr &&
					window->LastFrameActive == context.FrameCount && DrawsInColour(*window->DrawList, colour))
				{
					return true;
				}
			}

			return false;
		}

		// The repository changed under the window, as a fetch changes it, and the model's refresh has run.
		void Refresh()
		{
			Events.Publish(Graphite::RepositoryChangedEvent{});
			Model.OnUpdate(Core::Timestep{Graphite::Debouncer::kMaxSeconds});
			Settle();
		}

		// Shows the history of MakeRead's repository's one tag as the reader does: the list scrolled to its end, the Tags
		// section opened, its one row clicked.
		void ClickTheOneTagRow()
		{
			ImGuiWindow* rows = SidebarRows();
			ImGui::SetScrollY(rows, rows->ScrollMax.y);
			Step();

			const float pitch = Graphite::ListRowPitch();
			const float tagsTop = YoursRowTop(kBranchCount);
			ClickAt(InSidebarRows(tagsTop + pitch * 0.5f));
			ClickAt(InSidebarRows(tagsTop + pitch * 1.5f));
		}

		// Shows the one stash's commit as the reader does: the list scrolled to its end, the stash's row clicked under the
		// open Stashes section, which follows the closed Tags section when the repository has tags, then the stash's one
		// commit clicked in its history.
		void ClickTheOneStashCommit(bool tagsAbove)
		{
			ImGuiWindow* rows = SidebarRows();
			ImGui::SetScrollY(rows, rows->ScrollMax.y);
			Step();

			const float pitch = Graphite::ListRowPitch();
			const float stashesTop = YoursRowTop(kBranchCount) + (tagsAbove ? pitch : 0.0f);
			ClickAt(InSidebarRows(stashesTop + pitch * 1.5f));
			Settle();

			const ImGuiWindow* history = WindowNamed("##history");
			REQUIRE(history != nullptr);
			ClickAt(ImVec2(history->Pos.x + 60.0f, history->DC.CursorStartPos.y + pitch * 0.5f));
			Settle();
		}

		// The window drawn last frame whose name, inside this one's, holds `child`.
		const ImGuiWindow* WindowNamed(const char* child) const
		{
			const ImGuiContext& context = *ImGui::GetCurrentContext();

			for (const ImGuiWindow* window : context.Windows)
			{
				if (std::strstr(window->Name, WindowName) != nullptr && std::strstr(window->Name, child) != nullptr &&
					window->LastFrameActive == context.FrameCount)
				{
					return window;
				}
			}

			return nullptr;
		}

		// The text the commit details drew last frame, line by line and without spaces, in the window's own face.
		std::vector<DrawnLine> DetailsText() const
		{
			const ImGuiContext& context = *ImGui::GetCurrentContext();
			std::vector<DrawnLine> lines;

			for (const ImGuiWindow* window : context.Windows)
			{
				if (std::strstr(window->Name, WindowName) != nullptr && std::strstr(window->Name, "##gitDiff") != nullptr &&
					window->LastFrameActive == context.FrameCount)
				{
					const std::vector<DrawnLine> drawn = DrawnTextLines(*window->DrawList, *Font);
					lines.insert(lines.end(), drawn.begin(), drawn.end());
				}
			}

			std::ranges::stable_sort(lines, {}, &DrawnLine::Top);
			return lines;
		}

		// The line reads asked while they were held are made and land.
		void ReleaseLineReads()
		{
			HoldsLineReads = false;
			HeldLineReads.RunAll();
			Settle();
		}

		// The history's list as drawn last frame; null when it drew none.
		const ImGuiTable* HistoryTable() const
		{
			ImGuiContext& context = *ImGui::GetCurrentContext();

			for (int index = 0; index < context.Tables.GetMapSize(); ++index)
			{
				const ImGuiTable* table = context.Tables.TryGetMapData(index);

				if (table != nullptr && table->LastFrameActive == context.FrameCount && table->OuterWindow != nullptr &&
					std::strstr(table->OuterWindow->Name, WindowName) != nullptr &&
					std::strstr(table->OuterWindow->Name, "##gitCommits") != nullptr)
				{
					return table;
				}
			}

			return nullptr;
		}

		// How many rows the history's list drew last frame: its commits and the Local changes, banner and fold rows.
		int HistoryRowCount() const
		{
			const ImGuiTable* table = HistoryTable();
			return table != nullptr ? table->CurrentRow + 1 : 0;
		}

		// Whether the history's header draws its vs button: the pointer goes along the header row until it is over it.
		bool HeaderShowsVsButton()
		{
			constexpr int kHistoryColumn = 1;
			constexpr float kStep = 4.0f;

			const ImGuiWindow* window = ImGui::FindWindowByName(WindowName);
			const ImGuiTable* table = ImGui::TableFindByID(ImGui::GetIDWithSeed("gitWindowA", nullptr, window->ID));

			if (table == nullptr)
			{
				return false;
			}

			// The header is the first line of the history's cell, the vs button as high as a frame.
			const ImGuiID vsButton = ImGui::GetIDWithSeed("##vsButton", nullptr, table->ID);
			const float middle = table->RowPosY1 + table->RowCellPaddingY + ImGui::GetFrameHeight() * 0.5f;
			const float left = table->Columns[kHistoryColumn].MinX;
			const float right = table->Columns[kHistoryColumn].MaxX;

			for (float x = left; x < right; x += kStep)
			{
				ImGui::GetIO().AddMousePosEvent(x, middle);
				Step();

				if (ImGui::GetCurrentContext()->HoveredId == vsButton)
				{
					return true;
				}
			}

			return false;
		}

		ImGuiWindow* SidebarRows() const
		{
			for (ImGuiWindow* window : ImGui::GetCurrentContext()->Windows)
			{
				if (std::strstr(window->Name, WindowName) != nullptr && std::strstr(window->Name, "SidebarRows") != nullptr)
				{
					return window;
				}
			}

			return nullptr;
		}

		const char* WindowName;
		Core::HeadState Head;
		ImGuiFrames Frames;

		Graphite::LineRead Read;
		FixedHeadPort Port;
		OneReadLinesPort LinesPort{Read};
		ListedWorktreesPort WorktreePort;
		NoContentPort ContentPort;

		// While held, a line read asked waits here unmade, as one still being made does.
		bool HoldsLineReads = false;
		DeferredJobs HeldLineReads;

		Core::JobSubmitter LineReadJobs()
		{
			return [this](std::function<void()> job) {
				if (HoldsLineReads)
				{
					HeldLineReads.Submitter()(std::move(job));
					return;
				}

				job();
			};
		}

		// While held, what the model is asked to read waits here unread, as a read still on its way does.
		bool HoldsModelReads = false;
		DeferredJobs HeldModelReads;

		Core::JobSubmitter ModelJobs()
		{
			return [this](std::function<void()> job) {
				if (HoldsModelReads)
				{
					HeldModelReads.Submitter()(std::move(job));
					return;
				}

				job();
			};
		}

		Graphite::GitModel Model{Port, ModelJobs()};
		Graphite::LinesModel Lines{LinesPort, LineReadJobs(), [] { return LineReadBuilder::Now().time_since_epoch().count(); }};
		// While held, a network operation asked waits here unrun, as one still running does.
		bool HoldsNetworkJobs = false;
		DeferredJobs HeldNetworkJobs;

		Core::JobSubmitter NetworkJobs()
		{
			return [this](std::function<void()> job) {
				if (HoldsNetworkJobs)
				{
					HeldNetworkJobs.Submitter()(std::move(job));
					return;
				}

				job();
			};
		}

		Graphite::NetworkModel Network{Port, Model, NetworkJobs()};
		// While held, a worktree list asked waits here unread, as one of dozens of worktrees still being read does.
		bool HoldsWorktreeReads = false;
		DeferredJobs HeldWorktreeReads;

		Core::JobSubmitter WorktreeJobs()
		{
			return [this](std::function<void()> job) {
				if (HoldsWorktreeReads)
				{
					HeldWorktreeReads.Submitter()(std::move(job));
					return;
				}

				job();
			};
		}

		Graphite::WorktreesModel Worktrees{WorktreePort, WorktreeJobs(), [] { return std::int64_t{0}; }};
		Graphite::ContentModel Content{ContentPort, RunJobsInline()};

		Graphite::ModalGate Modals;
		Core::EventBus Events;
		FakeEditorBuffers Buffers;
		Graphite::ActiveScheme Scheme;
		Graphite::ChangesShared Shared;
		Graphite::ChangesView Changes{Model, Modals, Buffers, Scheme, Events, Shared, "window"};

		WorktreeStatusPort WorktreeGitPort;
		Graphite::WorktreeChanges WorktreeChanges{WorktreeGitPort, [](const std::filesystem::path&) {}, RunJobsInline(),
												  RunJobsInline(), [](const std::filesystem::path&) -> Core::Scope<Graphite::WorktreeWatch> {
													  return Core::CreateScope<QuietWatch>();
												  }};
		Graphite::ChangesView WorktreeView{WorktreeChanges.Model(), Modals, Buffers, Scheme, Events, WorktreeChanges.Draft(),
										   "worktree"};
		Graphite::WriteConsequences Writes{Model, Buffers, Shared, [](const Graphite::GitModel::WriteFailure&) {}, [] {}};
		Graphite::RepositorySession Repository{Model, Writes, Network, Lines, Worktrees, Content};
		TempAppSettings Settings;
		Graphite::EditorSettings EditorPreferences{Settings.Get()};
		Graphite::DiffWindows Diffs;

		Graphite::GitView View{Model,  Lines,   Network, Worktrees, Content, Changes, WorktreeChanges, WorktreeView, Scheme, EditorPreferences,
							   Diffs, Modals, Buffers,   Events};

		float BarTop = 0.0f;
		ImGuiID ChipId = 0;
		ImGuiID FetchId = 0;
		ImGuiID PushId = 0;
		ImFontBaked* Font = nullptr; // the window's face, as its text is drawn in
		ImFontBaked* AgeFont = nullptr; // the face the bar's "fetched … ago" is drawn in

		// The bar's last-fetch age as drawn last frame, without spaces, from "fetched" on; empty when it draws none.
		std::string BarAge() const
		{
			const ImGuiContext& context = *ImGui::GetCurrentContext();

			for (const ImGuiWindow* window : context.Windows)
			{
				const bool ours = std::strstr(window->Name, WindowName) != nullptr && window->LastFrameActive == context.FrameCount;

				if (!ours)
				{
					continue;
				}

				for (const DrawnLine& line : DrawnTextLines(*window->DrawList, *AgeFont))
				{
					if (const std::size_t age = line.Text.find("fetched"); age != std::string::npos)
					{
						return line.Text.substr(age);
					}
				}
			}

			return {};
		}
	};

	// Whether `middle` lies in the row that starts `top` down the sidebar's list and is `height` high.
	bool InSlot(std::optional<float> middle, float top, float height)
	{
		return middle.has_value() && *middle > top && *middle < top + height;
	}

	// main at its tip and beside it x, merged into main, with a tag of x's short name at x's tip.
	Graphite::LineRead MergedBranchRead()
	{
		LineReadBuilder builder;
		const std::size_t main = builder.AddLocal("main", {.Head = true});
		builder.MakeBase(main);
		const std::size_t merged = builder.AddLocal("x", {.State = Core::BranchState::MergedByHistory});
		builder.Compare(merged, main);

		Graphite::LineRead read = builder.Build();
		read.Model.Branches.front().Kind = Core::ComparedKind::Base;
		read.Graph = Core::CreateRef<const Core::AncestryGraph>();
		return read;
	}

	// main at its tip and beside it x, compared with main, whose history this shallow clone cuts off before the two meet.
	Graphite::LineRead CutOffBranchRead()
	{
		LineReadBuilder builder;
		const std::size_t main = builder.AddLocal("main", {.Head = true});
		builder.MakeBase(main);
		const std::size_t cutOff = builder.AddLocal("x", {.State = Core::BranchState::HistoryCutOff});
		builder.Compare(cutOff, main);

		Graphite::LineRead read = builder.Build();
		read.Model.Branches.front().Kind = Core::ComparedKind::Base;
		read.Graph = Core::CreateRef<const Core::AncestryGraph>();
		return read;
	}

	// The window showing x of CutOffBranchRead, which git is found for, with its banner over its one commit; by default
	// the repository has a remote to fetch the full history from.
	void ShowCutOffBranch(GitWindow& window, std::vector<Core::Remote> remotes = {{"origin", "C:/server.git"}})
	{
		window.Port.Network.Setup.Git = std::filesystem::path{"git.exe"};
		window.Port.Remotes = std::move(remotes);
		window.View.RestoreWindowState(Graphite::GitWindowState{.Ref = "refs/heads/x"});
		window.ListAgain();
	}

	// The text the history's list drew last frame, line by line and without spaces.
	std::vector<DrawnLine> HistoryText(const GitWindow& window)
	{
		const ImGuiTable* table = window.HistoryTable();
		REQUIRE(table != nullptr);
		return DrawnTextLines(*table->InnerWindow->DrawList, *window.Font);
	}

	// The middle of the action's words where the banner row drew them: x is not checked out, so the banner is the list's
	// first line of text.
	ImVec2 BannerActionPoint(const GitWindow& window)
	{
		const std::vector<DrawnLine> lines = HistoryText(window);
		REQUIRE_FALSE(lines.empty());

		const std::optional<std::pair<float, float>> action = HorizontalSpanOf(lines.front(), "Fetchfullhistory");
		REQUIRE(action.has_value());

		return ImVec2((action->first + action->second) * 0.5f, lines.front().Top + window.Font->Size * 0.5f);
	}
}

TEST_CASE("Git window: a shallow clone's banner draws Fetch full history after its sentence")
{
	GitWindow window("CutOffBannerWindow", HeadOnMain(), {}, CutOffBranchRead());
	ShowCutOffBranch(window);

	const std::vector<DrawnLine> lines = HistoryText(window);
	REQUIRE_FALSE(lines.empty());
	CHECK(lines.front().Text.starts_with("Historycutoffhere"));
	CHECK(lines.front().Text.ends_with("Fetchfullhistory"));
}

TEST_CASE("Git window: a click on a shallow clone's Fetch full history fetches the full history")
{
	GitWindow window("CutOffActionWindow", HeadOnMain(), {}, CutOffBranchRead());
	ShowCutOffBranch(window);
	REQUIRE(window.Port.Network.Calls.empty());

	window.ClickAt(BannerActionPoint(window));

	CHECK(window.Port.Network.Calls == std::vector<std::string>{"FetchFullHistory"});
}

TEST_CASE("Git window: in a folder not trusted Fetch and Fetch full history run nothing, and trusting the folder enables both at once")
{
	GitWindow window("UntrustedNetworkWindow", HeadOnMain(), {}, CutOffBranchRead());
	bool trusted = false;
	window.View.IsFolderTrusted = [&trusted] { return trusted; };
	ShowCutOffBranch(window);

	window.ClickAt(BannerActionPoint(window));
	REQUIRE(window.PointAt(window.FetchId));
	window.Click();
	window.Settle(2);

	CHECK(window.Port.Network.Calls.empty());
	CHECK_FALSE(window.Network.GetState().Running.has_value());

	// The trust is read each frame: nothing else changes, and both run.
	trusted = true;
	window.Settle(2);

	window.ClickAt(BannerActionPoint(window));
	CHECK(window.Port.Network.Calls == std::vector<std::string>{"FetchFullHistory"});

	window.Settle(2);
	REQUIRE(window.PointAt(window.FetchId));
	window.Click();
	REQUIRE(window.Port.Network.Calls.size() >= 2);
	CHECK(window.Port.Network.Calls[1] == "FetchAll");
}

TEST_CASE("Git window: without a remote a shallow clone's Fetch full history is disabled, as Fetch is, and a click fetches nothing")
{
	GitWindow window("CutOffNoRemoteWindow", HeadOnMain(), {}, CutOffBranchRead());
	ShowCutOffBranch(window, {});

	window.ClickAt(BannerActionPoint(window));
	window.Settle(2);

	CHECK(window.Port.Network.Calls.empty());
	CHECK_FALSE(window.Network.GetState().Running.has_value());
}

TEST_CASE("Git window: a running full history shows as the running Fetch button, and a click on it cancels the full history")
{
	GitWindow window("CutOffCancelWindow", HeadOnMain(), {}, CutOffBranchRead());
	ShowCutOffBranch(window);
	window.HoldsNetworkJobs = true;

	window.ClickAt(BannerActionPoint(window));
	window.Settle(2);

	const Graphite::NetworkState running = window.Network.GetState();
	REQUIRE(running.Running == Graphite::NetworkOperation::FullHistory);
	REQUIRE(running.Cancellable);

	REQUIRE(window.PointAt(window.FetchId));
	window.Click();

	window.HoldsNetworkJobs = false;
	window.HeldNetworkJobs.RunAll();
	window.Settle(2);

	// The job, cancelled before it started, ran no git.
	const std::optional<Graphite::NetworkDone> done = window.Network.GetShownDone();
	REQUIRE(done.has_value());
	CHECK(done->Cancelled);
	CHECK(done->Operation == Graphite::NetworkOperation::FullHistory);
	CHECK(window.Port.Network.Calls.empty());
}

TEST_CASE("Git window: clicking the head chip shows HEAD's history with its row selected and in view in that frame")
{
	GitWindow window("HeadChipWindow");
	window.Settle();

	window.View.RestoreWindowState(Graphite::GitWindowState{.Ref = "refs/heads/feature"});
	window.Settle();
	REQUIRE(window.View.CaptureWindowState().Ref == "refs/heads/feature");

	const float two = Graphite::SidebarTwoLineRowHeight();
	REQUIRE(InSlot(window.SelectedRowMiddle(), YoursRowTop(1), two));

	ImGuiWindow* rows = window.SidebarRows();
	REQUIRE(rows != nullptr);
	REQUIRE(rows->ScrollMax.y > 0.0f);

	// The reader scrolls the list to its end, away from Yours, where main's row is the first.
	ImGui::SetScrollY(rows, rows->ScrollMax.y);
	window.Step();
	REQUIRE(rows->Scroll.y > 0.0f);

	REQUIRE(window.PointAtChip());
	window.Click();

	// In the frame the click lands in: HEAD's history, and the list at its top, where main's row stands selected.
	CHECK(window.View.CaptureWindowState().Ref.empty());
	CHECK(rows->Scroll.y == 0.0f);
	CHECK(InSlot(window.SelectedRowMiddle(), YoursRowTop(0), two));

	// The reader scrolls away again; nothing scrolls the list back.
	ImGui::SetScrollY(rows, rows->ScrollMax.y);
	window.Settle();

	CHECK(rows->Scroll.y == doctest::Approx(rows->ScrollMax.y));
}

TEST_CASE("Git window: a click on Fetch starts a fetch")
{
	GitWindow window("NetworkFetchWindow");
	window.Port.Remotes = {{"origin", "https://example.com/a.git"}};
	window.Port.Network.Setup.Git = std::filesystem::path{"git.exe"};
	window.ListAgain();

	REQUIRE(window.PointAt(window.FetchId));
	window.Click();

	REQUIRE_FALSE(window.Port.Network.Calls.empty());
	CHECK(window.Port.Network.Calls.front() == "FetchAll");
}

TEST_CASE("Git window: a click on the running button cancels the operation the button showed")
{
	GitWindow window("NetworkCancelWindow");
	window.Port.Remotes = {{"origin", "https://example.com/a.git"}};
	window.Port.Network.Setup.Git = std::filesystem::path{"git.exe"};
	window.ListAgain();
	window.HoldsNetworkJobs = true;

	REQUIRE(window.PointAt(window.FetchId));
	window.Click();
	window.Settle(2);

	const Graphite::NetworkState running = window.Network.GetState();
	REQUIRE(running.Running == Graphite::NetworkOperation::Fetch);
	REQUIRE(running.Cancellable);

	// The button now cancels: a second click ends the operation the drawn state carried.
	REQUIRE(window.PointAt(window.FetchId));
	window.Click();

	window.HoldsNetworkJobs = false;
	window.HeldNetworkJobs.RunAll();
	window.Settle(2);

	const std::optional<Graphite::NetworkDone> done = window.Network.GetShownDone();
	REQUIRE(done.has_value());
	CHECK(done->Cancelled);
	CHECK(done->Operation == Graphite::NetworkOperation::Fetch);
}

TEST_CASE("Git window: after a fetch that failed once git had written FETCH_HEAD, the bar keeps the age of the fetch before it")
{
	const std::chrono::sys_seconds now = std::chrono::floor<std::chrono::seconds>(std::chrono::system_clock::now());

	GitWindow window("FailedFetchAgeWindow");
	window.Port.Remotes = {{"origin", "C:/server.git"}};
	window.Port.Network.Setup.Git = std::filesystem::path{"git.exe"};
	window.Port.Network.LastFetch = now - std::chrono::hours{2};
	window.ListAgain();

	const std::string ageBefore = window.BarAge();
	REQUIRE(ageBefore == "fetched2hago");

	window.Port.Network.FetchHeadWrittenAt = now - std::chrono::minutes{1};
	window.Port.Network.Outcome = Core::NetworkOutcome{
		.Result = Core::NetworkResult::Failed,
		.Failure = {.Lines = {"fatal: 'C:/server.git' does not appear to be a git repository"},
					.Errors = "fatal: 'C:/server.git' does not appear to be a git repository\n"}};

	REQUIRE(window.PointAt(window.FetchId));
	window.Click();
	window.ListAgain();

	// The listing reads the time the failed fetch wrote; the bar keeps the good fetch's.
	REQUIRE(window.Model.GetLastFetch() == std::optional<std::chrono::sys_seconds>{now - std::chrono::minutes{1}});
	CHECK(window.BarAge() == ageBefore);

	// FETCH_HEAD's time changing again, as a fetch in a terminal changes it, is shown.
	window.Port.Network.LastFetch = now - std::chrono::minutes{30};
	window.ListAgain();
	CHECK(window.BarAge() == "fetched30mago");
}

TEST_CASE("Git window: push with no upstream and several remotes asks which remote")
{
	GitWindow window("NetworkPushWindow");
	window.Port.Remotes = {{"origin", "https://example.com/a.git"}, {"backup", "ssh://host/a.git"}};
	window.Port.Network.Setup.Git = std::filesystem::path{"git.exe"};
	window.ListAgain();

	REQUIRE(window.PointAt(window.PushId));
	window.Click();

	// Asking pushes nothing yet: the menu names the branch and lists the remotes in the order the port lists them, which
	// the app's does by name and this one as given.
	CHECK(window.Port.Network.Calls.empty());

	// A popup is sized by its content over its first frames.
	window.Settle(3);

	const ImGuiContext& context = *ImGui::GetCurrentContext();
	REQUIRE(context.OpenPopupStack.Size == 1);
	const ImGuiWindow* menu = context.OpenPopupStack[0].Window;
	REQUIRE(menu != nullptr);

	// The last item is the second remote: a click on it pushes there and sets it as the upstream.
	const float itemHeight = ImGui::GetTextLineHeightWithSpacing();
	const ImVec2 target(menu->Pos.x + menu->Size.x * 0.5f, menu->Pos.y + menu->Size.y - menu->WindowPadding.y - itemHeight * 0.5f);
	window.ClickAt(target);

	REQUIRE_FALSE(window.Port.Network.Calls.empty());
	CHECK(window.Port.Network.Calls.back() == "PushTo backup (set upstream)");
}

TEST_CASE("Git window: clicking a detached head's chip shows HEAD's history and leaves the sidebar where it was")
{
	GitWindow window("DetachedChipWindow", DetachedHead());
	window.Settle();

	window.View.RestoreWindowState(Graphite::GitWindowState{.Ref = "refs/heads/feature"});
	window.Settle();
	REQUIRE(window.View.CaptureWindowState().Ref == "refs/heads/feature");

	ImGuiWindow* rows = window.SidebarRows();
	REQUIRE(rows != nullptr);
	REQUIRE(rows->ScrollMax.y > 0.0f);

	const float scrolled = std::floor(rows->ScrollMax.y * 0.5f);
	ImGui::SetScrollY(rows, scrolled);
	window.Step();
	REQUIRE(rows->Scroll.y == doctest::Approx(scrolled));

	REQUIRE(window.PointAtChip());
	window.Click();

	CHECK(window.View.CaptureWindowState().Ref.empty());
	CHECK(rows->Scroll.y == doctest::Approx(scrolled));

	window.Settle();

	CHECK(rows->Scroll.y == doctest::Approx(scrolled));
}

TEST_CASE("Git window: clicking the head chip clears a search that hides HEAD's row and opens the section the reader closed")
{
	GitWindow window("HeadChipRevealWindow");
	window.Settle();

	window.View.RestoreWindowState(Graphite::GitWindowState{.Ref = "refs/heads/feature"});
	window.Settle();

	const float pitch = Graphite::ListRowPitch();
	const float two = Graphite::SidebarTwoLineRowHeight();
	REQUIRE(InSlot(window.SelectedRowMiddle(), YoursRowTop(1), two));

	// The reader closes Yours, then searches for the topics: the search lists Yours open, without main's row.
	window.ClickAt(window.InSidebarRows(pitch * 0.5f));
	REQUIRE_FALSE(window.SelectedRowMiddle().has_value());

	const ImGuiWindow* rows = window.SidebarRows();
	REQUIRE(rows != nullptr);

	// The search field stands right above the rows.
	window.ClickAt(ImVec2(rows->Pos.x + 60.0f, rows->Pos.y - Graphite::WindowBarEm(20.0f)));
	ImGui::GetIO().AddInputCharactersUTF8("topic");
	window.Step();
	window.Step();
	REQUIRE_FALSE(window.SelectedRowMiddle().has_value());

	REQUIRE(window.PointAtChip());
	window.Click();

	CHECK(window.View.CaptureWindowState().Ref.empty());
	CHECK(InSlot(window.SelectedRowMiddle(), YoursRowTop(0), two));

	// Cleared, the search lists every branch again, and Yours stays open.
	window.Settle();
	CHECK(InSlot(window.SelectedRowMiddle(), YoursRowTop(0), two));
}

TEST_CASE("Git window: clicking a tag row shows the tag's history with the row selected")
{
	GitWindow window("TagRowWindow", HeadOnMain(), {TagAt("v1", OidWithFirstByte(2))});
	window.Settle();

	// The Tags section comes closed, after Yours; opened, it lists the tag's row under its header.
	window.ClickTheOneTagRow();

	const float pitch = Graphite::ListRowPitch();
	CHECK(window.View.CaptureWindowState().Ref == "refs/tags/v1");
	CHECK(InSlot(window.SelectedRowMiddle(), YoursRowTop(kBranchCount) + pitch, pitch));

	REQUIRE(window.Model.GetShownLogRequest().has_value());
	CHECK(window.Model.GetShownLogRequest()->Ref == std::optional<std::string>{"refs/tags/v1"});
}

TEST_CASE("Git view: a shown tag deleted by a fetch falls back to HEAD")
{
	GitWindow window("TagDeletedWindow", HeadOnMain(), {TagAt("v1", OidWithFirstByte(2))});
	window.Settle();
	window.ClickTheOneTagRow();
	REQUIRE(window.View.CaptureWindowState().Ref == "refs/tags/v1");

	// The fetch moves no branch: only the tags are listed anew.
	window.Port.Tags.clear();
	window.Refresh();

	CHECK(window.View.CaptureWindowState().Ref.empty());
	REQUIRE(window.Model.GetShownLogRequest().has_value());
	CHECK_FALSE(window.Model.GetShownLogRequest()->Ref.has_value());
	REQUIRE(window.Model.GetLog() != nullptr);
	REQUIRE_FALSE(window.Model.GetLog()->Commits.empty());
	CHECK(window.Model.GetLog()->Commits.front().Id == OidWithFirstByte(1));
}

TEST_CASE("Git view: a shown tag moved by a fetch shows its new commit's history")
{
	GitWindow window("TagMovedWindow", HeadOnMain(), {TagAt("v1", OidWithFirstByte(2))});
	window.Settle();
	window.ClickTheOneTagRow();
	REQUIRE(window.View.CaptureWindowState().Ref == "refs/tags/v1");
	REQUIRE(window.Model.GetLog() != nullptr);
	REQUIRE_FALSE(window.Model.GetLog()->Commits.empty());
	REQUIRE(window.Model.GetLog()->Commits.front().Id == OidWithFirstByte(2));

	// The fetch moves the tag alone: the branches keep their snapshot, so only the tags' own listing can tell the view.
	window.Port.Tags = {TagAt("v1", OidWithFirstByte(40))};
	window.Refresh();

	CHECK(window.View.CaptureWindowState().Ref == "refs/tags/v1");
	REQUIRE(window.Model.GetShownLogRequest().has_value());
	CHECK(window.Model.GetShownLogRequest()->Ref == std::optional<std::string>{"refs/tags/v1"});
	REQUIRE(window.Model.GetLog() != nullptr);
	REQUIRE_FALSE(window.Model.GetLog()->Commits.empty());
	CHECK(window.Model.GetLog()->Commits.front().Id == OidWithFirstByte(40));
}

TEST_CASE("Git view: a tag's history does not wait for the line read, and has no fold row, vs button, banner or Local changes row")
{
	// The tag has the short name of x, a branch merged into main whose history shows main's newer commits beside it.
	GitWindow window("TagHistoryWindow", HeadOnMain(), {TagAt("x", OidWithFirstByte(2))}, MergedBranchRead());
	window.HoldsLineReads = true;
	window.Settle();
	REQUIRE(window.Lines.GetRead() == nullptr);

	window.View.RestoreWindowState(Graphite::GitWindowState{.Ref = "refs/tags/x"});
	window.Settle();

	// Asked and shown while the read is still being made.
	REQUIRE(window.Lines.GetRead() == nullptr);
	REQUIRE(window.Model.GetShownLogRequest().has_value());
	CHECK(window.Model.GetShownLogRequest()->Ref == std::optional<std::string>{"refs/tags/x"});
	REQUIRE(window.Model.GetLog() != nullptr);
	CHECK(window.HistoryRowCount() == 1);
	CHECK_FALSE(window.DrawsLocalChanges());

	// With the read in, the tag still has no line: its commit is its one row, under a header with no vs button.
	window.ReleaseLineReads();
	REQUIRE(window.Lines.GetRead() != nullptr);
	REQUIRE(window.Model.GetShownLogRequest().has_value());
	CHECK(window.Model.GetShownLogRequest()->Ref == std::optional<std::string>{"refs/tags/x"});
	CHECK(window.Model.GetShownLogRequest()->AlsoFrom.empty());
	CHECK_FALSE(window.Model.GetMarkedKind().has_value());
	CHECK(window.HistoryRowCount() == 1);
	CHECK_FALSE(window.DrawsLocalChanges());
	CHECK_FALSE(window.HeaderShowsVsButton());

	// The branch of the same short name has all of them but Local changes: its line beside it, the banner over its commit.
	window.View.RestoreWindowState(Graphite::GitWindowState{.Ref = "refs/heads/x"});
	window.Settle();
	REQUIRE(window.Model.GetShownLogRequest().has_value());
	CHECK(window.Model.GetShownLogRequest()->Ref == std::optional<std::string>{"refs/heads/x"});
	CHECK_FALSE(window.Model.GetShownLogRequest()->AlsoFrom.empty());
	CHECK(window.Model.GetMarkedKind() == std::optional<Graphite::GitModel::MarkedKind>{Graphite::GitModel::MarkedKind::Newer});
	CHECK(window.HistoryRowCount() == 2);
	CHECK(window.HeaderShowsVsButton());

	// And HEAD's history opens on its Local changes.
	window.View.RestoreWindowState(Graphite::GitWindowState{});
	window.Settle();
	CHECK(window.DrawsLocalChanges());
}

TEST_CASE("Git view: a tag a fetch adds is drawn as a tag pill on its commit's row and in its details")
{
	GitWindow window("TagPillWindow");
	window.Settle();

	// feature's history opens on its tip, whose details are shown beside the list.
	window.View.RestoreWindowState(Graphite::GitWindowState{.Ref = "refs/heads/feature"});
	window.Settle();

	const Graphite::GitPalette palette = Graphite::ReadGitPalette(window.Scheme.Get());
	REQUIRE(window.DrawsInColourIn("##gitCommits", palette.RefLocalFill));
	REQUIRE(window.DrawsInColourIn("##gitDiff", palette.RefLocalFill));
	REQUIRE_FALSE(window.DrawsInColourIn("##gitCommits", palette.RefTagFill));
	REQUIRE_FALSE(window.DrawsInColourIn("##gitDiff", palette.RefTagFill));

	// The fetch brings a tag on feature's tip and moves no branch.
	window.Port.Tags = {TagAt("v1", OidWithFirstByte(2))};
	window.Refresh();

	CHECK(window.DrawsInColourIn("##gitCommits", palette.RefTagFill));
	CHECK(window.DrawsInColourIn("##gitDiff", palette.RefTagFill));
}

namespace {

	// What the details of a stash's commit draw, the stash shown in a window of its own whose repository has `tags`.
	std::vector<DrawnLine> StashDetailsText(const char* windowName, std::vector<Core::Tag> tags)
	{
		const Core::Oid stash = OidWithFirstByte(60);

		GitWindow window(windowName, HeadOnMain(), std::move(tags));
		window.Port.Stashes = {Core::StashEntry{.Index = 0, .Message = "On main: wip", .Id = stash}};
		window.Model.RequestStashes();
		window.Settle();
		window.ClickTheOneStashCommit(!window.Port.Tags.empty());

		REQUIRE(window.Model.GetShownLogRequest().has_value());
		REQUIRE(window.Model.GetShownLogRequest()->Stash == std::optional<Core::Oid>{stash});
		return window.DetailsText();
	}
}

TEST_CASE("Git view: a stash's details draw First tag right under In bases, as every commit's details do")
{
	// The stash's commit is outside the read: in no base, and held by no tag.
	const std::vector<DrawnLine> lines = StashDetailsText("StashFirstTagWindow", {TagAt("v1", OidWithFirstByte(2))});
	const std::optional<std::size_t> inBases = LineStartingWith(lines, "Inbases");
	REQUIRE(inBases.has_value());
	CHECK(lines[*inBases].Text == "Inbasesinnobaseyet");
	REQUIRE(*inBases + 1 < lines.size());
	CHECK(lines[*inBases + 1].Text == "Firsttagnotagholdsityet");

	// Without tags the same stash's details have In bases alone.
	const std::vector<DrawnLine> withoutTags = StashDetailsText("StashNoTagsWindow", {});
	CHECK(LineStartingWith(withoutTags, "Inbasesinnobaseyet").has_value());
	CHECK_FALSE(LineStartingWith(withoutTags, "Firsttag").has_value());
}

TEST_CASE("Git view: a tag and a branch of one short name are restored in turn, each with its own row, pills and details")
{
	// x is a branch merged into main; the tag x is on a commit of its own.
	GitWindow window("SameNameWindow", HeadOnMain(), {TagAt("x", OidWithFirstByte(40))}, MergedBranchRead());
	window.Settle();
	const Graphite::GitPalette palette = Graphite::ReadGitPalette(window.Scheme.Get());

	window.View.RestoreWindowState(Graphite::GitWindowState{.Ref = "refs/tags/x"});
	window.Settle();
	REQUIRE(window.View.CaptureWindowState().Ref == "refs/tags/x");
	const std::optional<float> tagRow = window.SelectedRowMiddle();
	REQUIRE(tagRow.has_value());
	CHECK(window.DrawsInColourIn("##gitCommits", palette.RefTagFill));
	CHECK_FALSE(window.DrawsInColourIn("##gitCommits", palette.RefLocalFill));
	CHECK(LineStartingWith(window.DetailsText(), "Firsttag").has_value());

	window.View.RestoreWindowState(Graphite::GitWindowState{.Ref = "refs/heads/x"});
	window.Settle();
	REQUIRE(window.View.CaptureWindowState().Ref == "refs/heads/x");
	const std::optional<float> branchRow = window.SelectedRowMiddle();
	REQUIRE(branchRow.has_value());
	CHECK(window.DrawsInColourIn("##gitCommits", palette.RefLocalFill));
	CHECK_FALSE(window.DrawsInColourIn("##gitCommits", palette.RefTagFill));
	CHECK(LineStartingWith(window.DetailsText(), "Firsttag").has_value());

	// The branch's row is above the tag's, which is in the Tags section below Yours: two rows, each selected by itself.
	CHECK(*branchRow < *tagRow);

	window.View.RestoreWindowState(Graphite::GitWindowState{.Ref = "refs/tags/x"});
	window.Settle();
	REQUIRE(window.SelectedRowMiddle().has_value());
	CHECK(*window.SelectedRowMiddle() == doctest::Approx(*tagRow));
	CHECK(window.DrawsInColourIn("##gitCommits", palette.RefTagFill));
	CHECK_FALSE(window.DrawsInColourIn("##gitCommits", palette.RefLocalFill));
}

namespace {

	// Whether the sidebar draws a selected row and its middle is inside the part of the list on screen.
	bool SelectedRowIsInView(const GitWindow& window)
	{
		const std::optional<float> middle = window.SelectedRowMiddle();
		const ImGuiWindow* rows = window.SidebarRows();

		return middle.has_value() && rows != nullptr && *middle >= rows->Scroll.y &&
			   *middle <= rows->Scroll.y + rows->InnerRect.GetHeight();
	}
}

TEST_CASE("Git view: an opening with a restored tag in a closed folder opens its section and folder, while the lines are read and after")
{
	GitWindow window("TagInFolderWindow", HeadOnMain(), {TagAt("release/1.0", OidWithFirstByte(40)), TagAt("v2", OidWithFirstByte(41))},
					 MergedBranchRead());
	window.HoldsLineReads = true;
	window.View.RestoreWindowState(Graphite::GitWindowState{.Ref = "refs/tags/release/1.0"});
	window.Settle();
	REQUIRE(window.Lines.GetRead() == nullptr);

	// Neither the Tags section nor the folder was opened by the user, yet the tag's row is drawn selected and in view.
	REQUIRE(window.View.CaptureWindowState().Ref == "refs/tags/release/1.0");
	CHECK(SelectedRowIsInView(window));

	window.ReleaseLineReads();
	REQUIRE(window.Lines.GetRead() != nullptr);
	CHECK(window.View.CaptureWindowState().Ref == "refs/tags/release/1.0");
	CHECK(SelectedRowIsInView(window));
}

namespace {

	// Whether the sidebar drew its one waiting row last frame, in place of the sections that need the lines.
	bool SidebarWaits(const GitWindow& window)
	{
		const ImGuiWindow* rows = window.SidebarRows();
		REQUIRE(rows != nullptr);

		for (const DrawnLine& line : DrawnTextLines(*rows->DrawList, *window.Font))
		{
			if (line.Text.find("Reading") != std::string::npos)
			{
				return true;
			}
		}

		return false;
	}

	// The lines are read and the worktree list is not: the sidebar waits with every section that needs the lines, and
	// builds them, HEAD's row selected in Yours, only once the list has answered.
	void CheckSidebarWaitsForWorktreeList(const char* windowName, bool listFails)
	{
		GitWindow window(windowName);
		window.HoldsWorktreeReads = true;
		window.Settle();

		REQUIRE(window.Lines.GetRead() != nullptr);
		REQUIRE_FALSE(window.Worktrees.HasListAnswered());
		CHECK(SidebarWaits(window));
		CHECK_FALSE(window.SelectedRowMiddle().has_value());

		if (listFails)
		{
			window.WorktreePort.ListFailure = Core::Error{Core::ErrorCode::IoFailure, "cannot list worktrees"};
		}

		window.HoldsWorktreeReads = false;
		window.HeldWorktreeReads.RunAll();
		window.Settle();

		REQUIRE(window.Worktrees.HasListAnswered());
		CHECK((window.Worktrees.Entries() == nullptr) == listFails);
		CHECK_FALSE(SidebarWaits(window));
		CHECK(InSlot(window.SelectedRowMiddle(), YoursRowTop(0), Graphite::SidebarTwoLineRowHeight()));
	}
}

TEST_CASE("Git window: the sidebar's sections wait for the worktree list as well as the lines, so the Worktrees section never pushes Bases down")
{
	CheckSidebarWaitsForWorktreeList("WorktreeListWaitWindow", false);
}

TEST_CASE("Git window: a worktree list that cannot be read holds the sidebar's sections no longer")
{
	CheckSidebarWaitsForWorktreeList("WorktreeListFailWindow", true);
}

namespace {

	const std::filesystem::path kLinkedFolder = "C:/linked";
	const std::filesystem::path kLooseFolder = "C:/loose";
	const Core::Oid kLooseHead = OidWithFirstByte(0x77);

	Core::WorktreeEntry LooseWorktree()
	{
		Core::WorktreeEntry entry;
		entry.Path = kLooseFolder;
		entry.Name = "loose";
		entry.Head.Kind = Core::HeadKind::Detached;
		entry.Head.Commit = kLooseHead;
		return entry;
	}

	// The open folder C:/repo on main, C:/linked on feature and C:/loose detached.
	std::vector<Core::WorktreeEntry> RepoLinkedAndLoose()
	{
		return {Graphite::Fixtures::WorktreeOn("main", "C:/repo"), Graphite::Fixtures::WorktreeOn("feature", kLinkedFolder),
				LooseWorktree()};
	}

	// main checked out and feature beside it, so the Worktrees section follows Yours' two rows in view.
	Graphite::LineRead MainAndFeatureRead()
	{
		LineReadBuilder builder;
		builder.AddLocal("main", {.Head = true, .AgeDays = 40});
		builder.AddLocal("feature", {.AgeDays = 2});

		Graphite::LineRead read = builder.Build();
		read.Graph = Core::CreateRef<const Core::AncestryGraph>();
		return read;
	}

	// The line the sidebar drew last frame that holds `text`; empty when none does.
	std::optional<DrawnLine> SidebarLineWith(const GitWindow& window, const std::string& text)
	{
		const ImGuiWindow* rows = window.SidebarRows();
		REQUIRE(rows != nullptr);

		for (const DrawnLine& line : DrawnTextLines(*rows->DrawList, *window.Font))
		{
			if (line.Text.find(text) != std::string::npos)
			{
				return line;
			}
		}

		return std::nullopt;
	}

	// Clicks the worktree row whose first line names `folder`, and lets the history it asks for land.
	void ClickWorktreeRow(GitWindow& window, const std::string& folder)
	{
		const std::optional<DrawnLine> line = SidebarLineWith(window, folder);
		REQUIRE(line.has_value());

		const ImGuiWindow* rows = window.SidebarRows();
		window.ClickAt(ImVec2(rows->Pos.x + 60.0f, line->Top + window.Font->Size * 0.5f));
		window.Settle();
	}

	// Whether any window of this one drew a line holding `text` last frame, in the window's own face.
	bool DrawsText(const GitWindow& window, const std::string& text)
	{
		const ImGuiContext& context = *ImGui::GetCurrentContext();

		for (const ImGuiWindow* drawn : context.Windows)
		{
			if (std::strstr(drawn->Name, window.WindowName) == nullptr || drawn->LastFrameActive != context.FrameCount)
			{
				continue;
			}

			for (const DrawnLine& line : DrawnTextLines(*drawn->DrawList, *window.Font))
			{
				if (line.Text.find(text) != std::string::npos)
				{
					return true;
				}
			}
		}

		return false;
	}

	// The history the model was last asked for is the open folder's HEAD's: no ref, no stash, no commit to start from.
	bool AsksHeadHistory(const GitWindow& window)
	{
		const std::optional<Core::LogRequest>& asked = window.Model.GetAskedLogRequest();
		return asked.has_value() && !asked->Ref.has_value() && !asked->Stash.has_value() && !asked->From.has_value();
	}

	// How far down the list's content the backgrounds the sidebar drew selected last frame start and end; empty when it
	// drew none.
	std::optional<std::pair<float, float>> SelectedSpan(const GitWindow& window)
	{
		const ImGuiWindow* rows = window.SidebarRows();
		REQUIRE(rows != nullptr);

		const ImU32 selected = ImGui::GetColorU32(Graphite::CurrentListRowColours().Selected);
		std::optional<std::pair<float, float>> span;

		for (const ImDrawVert& vertex : rows->DrawList->VtxBuffer)
		{
			if (vertex.col != selected)
			{
				continue;
			}

			const float down = vertex.pos.y - rows->DC.CursorStartPos.y;
			span = span.has_value() ? std::pair{std::min(span->first, down), std::max(span->second, down)} : std::pair{down, down};
		}

		return span;
	}

	// The sidebar draws exactly one row selected, whatever row it is: a second one would stretch the span past one row.
	bool DrawsOneRowSelected(const GitWindow& window)
	{
		const std::optional<std::pair<float, float>> span = SelectedSpan(window);
		return span.has_value() && span->second - span->first <= Graphite::SidebarTwoLineRowHeight() + 1.0f;
	}

	// The sidebar draws exactly one row selected, the one whose first line holds `text`.
	void CheckOnlyRowSelected(const GitWindow& window, const std::string& text)
	{
		const std::optional<DrawnLine> line = SidebarLineWith(window, text);
		REQUIRE(line.has_value());
		REQUIRE(DrawsOneRowSelected(window));

		const float top = line->Top - window.SidebarRows()->DC.CursorStartPos.y;
		const std::optional<std::pair<float, float>> span = SelectedSpan(window);
		CHECK(span->first < top);
		CHECK(span->second > top);
	}

	// The sidebar draws exactly one row selected, the branch row at `place` in Yours.
	void CheckOnlyYoursRowSelected(const GitWindow& window, int place)
	{
		REQUIRE(DrawsOneRowSelected(window));
		CHECK(InSlot(window.SelectedRowMiddle(), YoursRowTop(place), Graphite::SidebarTwoLineRowHeight()));
	}

	// The worktrees are read again, as for a new branch snapshot: a worktree removed or moved leaves the branches as they
	// were.
	void ListWorktreesAgain(GitWindow& window)
	{
		REQUIRE(window.Model.GetBranches() != nullptr);
		window.Worktrees.Follow(Core::CreateRef<const std::vector<Core::Branch>>(*window.Model.GetBranches()),
								window.Model.GetWorkingDirectory());
		window.Settle();
	}
}

TEST_CASE("Git window: with worktrees listed HEAD's history draws one row selected, HEAD's branch in Yours, not this folder's row too")
{
	GitWindow window("WorktreeDefaultSelectionWindow", HeadOnMain(), {}, MainAndFeatureRead());
	window.WorktreePort.Listed = RepoLinkedAndLoose();
	window.Settle();
	REQUIRE(SidebarLineWith(window, "thisfolder").has_value());

	CheckOnlyYoursRowSelected(window, 0);
}

TEST_CASE("Git window: a click on a branch worktree's row selects its branch, and on a detached one's shows the history from its HEAD")
{
	GitWindow window("WorktreeClickWindow", HeadOnMain(), {}, MainAndFeatureRead());
	window.WorktreePort.Listed = RepoLinkedAndLoose();
	window.Settle();
	REQUIRE(window.Worktrees.Entries() != nullptr);

	ClickWorktreeRow(window, "linked");

	CHECK(window.View.CaptureWindowState() == Graphite::GitWindowState{.Ref = "refs/heads/feature"});

	ClickWorktreeRow(window, "loose");

	const std::optional<Core::LogRequest>& shown = window.Model.GetShownLogRequest();
	REQUIRE(shown.has_value());
	CHECK(shown->From == kLooseHead);
	CHECK_FALSE(shown->Ref.has_value());
	CHECK(DrawsText(window, "HEADinloose"));
	CHECK(window.View.CaptureWindowState() == Graphite::GitWindowState{.Worktree = "C:/loose"});

	// A detached worktree's history has no Local changes row of the open folder's: its first row is its own changes.
	CHECK_FALSE(DrawsText(window, "Localchanges"));
	CHECK(DrawsText(window, "Changesinloose"));

	// The row clicked is the one drawn selected, and the only one.
	CheckOnlyRowSelected(window, "loose");

	// The open folder's own row shows its HEAD's history again, HEAD's branch in Yours the row selected.
	ClickWorktreeRow(window, "repo");
	CHECK(window.View.CaptureWindowState() == Graphite::GitWindowState{});
	CHECK(AsksHeadHistory(window));

	CheckOnlyYoursRowSelected(window, 0);
}

TEST_CASE("Git window: a branch opened from its worktree's row draws that row selected alone, and from Yours its own row alone")
{
	GitWindow window("WorktreeBranchSelectionWindow", HeadOnMain(), {}, MainAndFeatureRead());
	window.WorktreePort.Listed = RepoLinkedAndLoose();
	window.Settle();

	ClickWorktreeRow(window, "linked");
	REQUIRE(window.View.CaptureWindowState() == Graphite::GitWindowState{.Ref = "refs/heads/feature"});
	CheckOnlyRowSelected(window, "linked");

	// The same branch clicked in Yours is selected there.
	window.ClickAt(window.InSidebarRows(YoursRowTop(1) + Graphite::SidebarTwoLineRowHeight() * 0.5f));
	window.Settle();
	REQUIRE(window.View.CaptureWindowState() == Graphite::GitWindowState{.Ref = "refs/heads/feature"});

	CheckOnlyYoursRowSelected(window, 1);
}

TEST_CASE("Git window: a selected detached worktree follows its HEAD to a new commit, and onto a branch")
{
	GitWindow window("WorktreeFollowsHeadWindow", HeadOnMain(), {}, MainAndFeatureRead());
	window.WorktreePort.Listed = RepoLinkedAndLoose();
	window.Settle();

	ClickWorktreeRow(window, "loose");
	REQUIRE(window.Model.GetAskedLogRequest().has_value());
	REQUIRE(window.Model.GetAskedLogRequest()->From == kLooseHead);

	// A commit made in the worktree: its history is walked from the new HEAD.
	const Core::Oid newHead = OidWithFirstByte(0x78);
	window.WorktreePort.Listed.back().Head.Commit = newHead;
	ListWorktreesAgain(window);

	REQUIRE(window.Model.GetAskedLogRequest().has_value());
	CHECK(window.Model.GetAskedLogRequest()->From == newHead);
	REQUIRE(window.Model.GetShownLogRequest().has_value());
	CHECK(window.Model.GetShownLogRequest()->From == newHead);
	CHECK(window.View.CaptureWindowState() == Graphite::GitWindowState{.Worktree = "C:/loose"});
	CHECK(DrawsText(window, "HEADinloose"));

	// The worktree switched onto feature, which the linked worktree let go of: the history is feature's, and the
	// worktree's row stays the one selected.
	window.WorktreePort.Listed = {Graphite::Fixtures::WorktreeOn("main", "C:/repo"), Graphite::Fixtures::WorktreeOn("feature", kLooseFolder)};
	ListWorktreesAgain(window);

	CHECK(window.View.CaptureWindowState() == Graphite::GitWindowState{.Ref = "refs/heads/feature"});
	CHECK_FALSE(DrawsText(window, "HEADinloose"));
	CheckOnlyRowSelected(window, "loose");
}

TEST_CASE("Git window: a selected worktree that leaves the list, or whose folder goes missing, falls back to the open folder's HEAD")
{
	for (const bool folderMissing : {false, true})
	{
		CAPTURE(folderMissing);

		GitWindow window(folderMissing ? "WorktreeMissingWindow" : "WorktreeGoneWindow", HeadOnMain(), {}, MainAndFeatureRead());
		window.WorktreePort.Listed = RepoLinkedAndLoose();
		window.Settle();

		ClickWorktreeRow(window, "loose");
		REQUIRE(window.Model.GetShownLogRequest().has_value());
		REQUIRE(window.Model.GetShownLogRequest()->From == kLooseHead);

		if (folderMissing)
		{
			// The folder is gone, so git can no longer read its HEAD.
			Core::WorktreeEntry missing = LooseWorktree();
			missing.FolderMissing = true;
			missing.Head = Core::HeadState{};
			window.WorktreePort.Listed.back() = missing;
		}
		else
		{
			window.WorktreePort.Listed.pop_back();
		}

		ListWorktreesAgain(window);

		CHECK(window.View.CaptureWindowState() == Graphite::GitWindowState{});
		CHECK(AsksHeadHistory(window));
		CHECK_FALSE(DrawsText(window, "HEADinloose"));
	}
}

TEST_CASE("Git window: a restored worktree shows its history once the worktree list is in, and one no longer listed restores HEAD's")
{
	{
		GitWindow window("WorktreeRestoredWindow", HeadOnMain(), {}, MainAndFeatureRead());
		window.WorktreePort.Listed = RepoLinkedAndLoose();
		window.HoldsWorktreeReads = true;
		window.View.RestoreWindowState(Graphite::GitWindowState{.Worktree = "C:/loose"});
		window.Settle();

		// Until the list is in it is not known whether the worktree is still there, so no history is asked.
		CHECK(window.View.CaptureWindowState() == Graphite::GitWindowState{.Worktree = "C:/loose"});
		CHECK_FALSE(window.Model.GetAskedLogRequest().has_value());

		window.HoldsWorktreeReads = false;
		window.HeldWorktreeReads.RunAll();
		window.Settle();

		const std::optional<Core::LogRequest>& asked = window.Model.GetAskedLogRequest();
		REQUIRE(asked.has_value());
		CHECK(asked->From == kLooseHead);
		CHECK(DrawsText(window, "HEADinloose"));
	}

	{
		GitWindow window("WorktreeRestoredGoneWindow", HeadOnMain(), {}, MainAndFeatureRead());
		window.WorktreePort.Listed = RepoLinkedAndLoose();
		window.View.RestoreWindowState(Graphite::GitWindowState{.Worktree = "C:/removed"});
		window.Settle();

		CHECK(window.View.CaptureWindowState() == Graphite::GitWindowState{});
		CHECK(AsksHeadHistory(window));
	}
}

namespace {

	// The history's first line of text drawn last frame.
	std::string FirstHistoryLine(const GitWindow& window)
	{
		const std::vector<DrawnLine> lines = HistoryText(window);
		REQUIRE_FALSE(lines.empty());
		return lines.front().Text;
	}

	// Clicks the history's row at `place`, 0 for its first.
	void ClickHistoryRow(GitWindow& window, int place)
	{
		const ImGuiWindow* list = window.WindowNamed("##history");
		REQUIRE(list != nullptr);

		const float pitch = Graphite::ListRowPitch();
		window.ClickAt(ImVec2(list->Pos.x + 60.0f, list->DC.CursorStartPos.y + pitch * (static_cast<float>(place) + 0.5f)));
		window.Settle();
	}

	// The items under the pointer moved down the history's first three rows, top to bottom.
	std::vector<ImGuiID> HistoryRowIds(GitWindow& window)
	{
		const ImGuiWindow* list = window.WindowNamed("##history");
		REQUIRE(list != nullptr);

		constexpr float kRowsWalked = 3.0f;
		const float top = list->DC.CursorStartPos.y;
		std::vector<ImGuiID> ids;

		for (const HoveredStretch& stretch : HoveredDown(window.Frames, list->Pos.x + 60.0f, top,
														  top + Graphite::ListRowPitch() * kRowsWalked,
														  [&window] { window.DrawWindow(); }))
		{
			ids.push_back(stretch.Id);
		}

		return ids;
	}

	// The line above the linked worktree's lists, as drawn: "Worktree · <its full path>" without its spaces and dot.
	std::string LinkedWhereLine()
	{
		return "Worktree" + Core::ToUtf8(Core::NormalizeDirectory(kLinkedFolder));
	}

	// The worktree view's commit button's width, read off the item under the pointer at the start of its line; empty when
	// nothing is there.
	std::optional<float> WorktreeCommitButtonWidth(GitWindow& window)
	{
		const ImGuiWindow* box = window.WindowNamed("commitMessage");
		REQUIRE(box != nullptr);

		const float middle = box->Pos.y + box->Size.y + ImGui::GetStyle().ItemSpacing.y + ImGui::GetFrameHeight() * 0.5f;
		const std::vector<HoveredStretch> stretches =
			HoveredAcross(window.Frames, middle, box->Pos.x, box->Pos.x + box->Size.x, [&window] { window.DrawWindow(); });

		if (stretches.empty())
		{
			return std::nullopt;
		}

		return stretches.front().To - stretches.front().From + 1.0f;
	}

	// Where the hover buttons of one.txt's row in the worktree view are, when it draws them.
	struct OneTxtRowButtons
	{
		std::optional<ImVec2> Stage;
		std::optional<ImVec2> Discard;
	};

	OneTxtRowButtons OneTxtRowButtonsNow(GitWindow& window)
	{
		const ImGuiWindow* list = window.WindowNamed("changes.lists");
		REQUIRE(list != nullptr);

		std::optional<float> rowMiddle;

		for (const DrawnLine& line : DrawnTextLines(*list->DrawList, *window.Font))
		{
			if (line.Text.find("one.txt") != std::string::npos)
			{
				rowMiddle = line.Top + window.Font->Size * 0.5f;
			}
		}

		REQUIRE(rowMiddle.has_value());

		// one.txt is the status's second entry, listed under CHANGES.
		constexpr int kOneTxtEntry = 1;
		const ImGuiID section = ImHashStr("CHANGES", 0, list->ID);
		const ImGuiID row = ImHashData(&kOneTxtEntry, sizeof(kOneTxtEntry), section);
		const ImGuiID stageId = ImHashStr("##stage", 0, row);
		const ImGuiID discardId = ImHashStr("##discard", 0, row);

		OneTxtRowButtons buttons;

		for (const HoveredStretch& stretch : HoveredAcross(window.Frames, *rowMiddle, list->Pos.x, list->Pos.x + list->Size.x,
														   [&window] { window.DrawWindow(); }))
		{
			if (stretch.Id == stageId)
			{
				buttons.Stage = ImVec2(stretch.Middle(), *rowMiddle);
			}

			if (stretch.Id == discardId)
			{
				buttons.Discard = ImVec2(stretch.Middle(), *rowMiddle);
			}
		}

		return buttons;
	}

	// The commit message box drawn last frame, whichever Changes view drew it; null when none was.
	const ImGuiWindow* CommitMessageBox(const GitWindow& window)
	{
		return window.WindowNamed("commitMessage");
	}
}

TEST_CASE("Git window: a branch checked out in another worktree shows Changes in that folder first, live, with its counts")
{
	GitWindow window("WorktreeChangesRowWindow", HeadOnMain(), {}, MainAndFeatureRead());
	window.WorktreePort.Listed = RepoLinkedAndLoose();
	window.Settle();

	ClickWorktreeRow(window, "linked");
	REQUIRE(window.View.CaptureWindowState() == Graphite::GitWindowState{.Ref = "refs/heads/feature"});

	CHECK(window.WorktreeChanges.Worktree() == kLinkedFolder);

	const std::string first = FirstHistoryLine(window);
	CHECK(first.find("Changesinlinked") != std::string::npos);
	CHECK(first.find("1staged") != std::string::npos);
	CHECK(first.find("2notstaged") != std::string::npos);
	CHECK(first.find("live") != std::string::npos);
	CHECK_FALSE(DrawsText(window, "Localchanges"));
}

TEST_CASE("Git window: selecting another worktree's changes row draws that worktree's Changes view, not the open folder's")
{
	GitWindow window("WorktreeChangesDetailsWindow", HeadOnMain(), {}, MainAndFeatureRead());
	window.WorktreePort.Listed = RepoLinkedAndLoose();
	window.Settle();

	ClickWorktreeRow(window, "linked");

	// The branch opens on its changes row, as the checked-out branch opens on Local changes.
	CHECK(DrawsText(window, LinkedWhereLine()));
	CHECK(DrawsText(window, "one.txt"));
	CHECK(DrawsText(window, "Committofeature"));

	// A commit selected shows that commit; the changes row selected again shows the worktree's view again.
	ClickHistoryRow(window, 1);
	CHECK_FALSE(DrawsText(window, LinkedWhereLine()));

	ClickHistoryRow(window, 0);
	CHECK(DrawsText(window, LinkedWhereLine()));
	CHECK(DrawsText(window, "two.txt"));
	CHECK_FALSE(DrawsText(window, "Nochanges"));

	// The empty message box names the target.
	CHECK(DrawsText(window, "Messageforfeatureinlinked"));

	// Without Stash the commit button takes the room beside Amend alone.
	window.WorktreeChanges.Draft().Message = "fix";
	window.Settle(1);
	const std::optional<float> commitWidth = WorktreeCommitButtonWidth(window);
	const ImGuiWindow* box = CommitMessageBox(window);
	REQUIRE(commitWidth.has_value());
	REQUIRE(box != nullptr);
	CHECK(*commitWidth == doctest::Approx(box->Size.x - Graphite::TickBoxWidth("Amend") - ImGui::GetStyle().ItemSpacing.x).epsilon(0.02));

	// Another process switched the worktree to another branch since the window read it: a stage is refused, and the row
	// offers no discard.
	window.WorktreeGitPort.Branch = "elsewhere";
	const OneTxtRowButtons buttons = OneTxtRowButtonsNow(window);
	CHECK_FALSE(buttons.Discard.has_value());
	REQUIRE(buttons.Stage.has_value());

	window.ClickAt(*buttons.Stage);
	window.Settle();

	const std::optional<Graphite::WorktreeWriteFailure> refused = window.WorktreeChanges.TakeWriteError();
	REQUIRE(refused.has_value());
	CHECK(refused->Failure.Error.Message.starts_with("Not staged:"));
}

TEST_CASE("Git window: a worktree in the middle of a merge names it on the line above its lists")
{
	GitWindow window("WorktreeMergingWindow", HeadOnMain(), {}, MainAndFeatureRead());
	window.WorktreePort.Listed = RepoLinkedAndLoose();
	window.WorktreeGitPort.Operation = Core::RepositoryOperation::Merge;
	window.Settle();

	ClickWorktreeRow(window, "linked");

	CHECK(DrawsText(window, LinkedWhereLine() + "MERGING"));
}

TEST_CASE("Git window: the open folder's HEAD still shows Local changes after another worktree's changes were shown")
{
	GitWindow window("WorktreeThenHeadWindow", HeadOnMain(), {}, MainAndFeatureRead());
	window.WorktreePort.Listed = RepoLinkedAndLoose();
	window.Settle();

	ClickWorktreeRow(window, "linked");
	ClickWorktreeRow(window, "repo");

	const std::string first = FirstHistoryLine(window);
	CHECK(first.find("Localchanges") != std::string::npos);
	CHECK(first.find("Changesin") == std::string::npos);
	CHECK_FALSE(DrawsText(window, LinkedWhereLine()));

	// No other worktree is shown, so none is followed.
	CHECK_FALSE(window.WorktreeChanges.Worktree().has_value());
}

TEST_CASE("Git window: typing in another worktree's commit box fills its own draft and leaves the side panel's alone")
{
	GitWindow window("WorktreeDraftWindow", HeadOnMain(), {}, MainAndFeatureRead());
	window.WorktreePort.Listed = RepoLinkedAndLoose();
	window.Settle();

	ClickWorktreeRow(window, "linked");
	REQUIRE(DrawsText(window, LinkedWhereLine()));

	const ImGuiWindow* box = CommitMessageBox(window);
	REQUIRE(box != nullptr);
	window.ClickAt(ImVec2(box->Pos.x + box->Size.x * 0.5f, box->Pos.y + box->Size.y * 0.5f));

	ImGui::GetIO().AddInputCharactersUTF8("wip");
	window.Settle(2);

	CHECK(window.WorktreeChanges.Draft().Message == "wip");
	CHECK(window.Shared.Message.empty());
}

TEST_CASE("Git window: off screen the worktree followed is kept unwatched, and its row comes back as it was")
{
	GitWindow window("WorktreeOffScreenWindow", HeadOnMain(), {}, MainAndFeatureRead());
	window.WorktreePort.Listed = RepoLinkedAndLoose();
	window.Settle();

	ClickWorktreeRow(window, "linked");
	REQUIRE(window.WorktreeChanges.Worktree() == kLinkedFolder);
	REQUIRE(window.WorktreeChanges.IsWatching());

	window.View.SetDrawnOnLatestFrame(false);
	window.View.OnUpdate(Core::Timestep{0.0f});

	CHECK(window.WorktreeChanges.Worktree() == kLinkedFolder);
	CHECK_FALSE(window.WorktreeChanges.IsWatching());

	// Back on screen, the first frame draws the counts it had, not a wait.
	window.Step();
	CHECK(FirstHistoryLine(window).find("1staged") != std::string::npos);

	window.Step();
	CHECK(window.WorktreeChanges.IsWatching());
}

TEST_CASE("Git window: a selected detached worktree whose HEAD moved keeps its rows on screen until the page from the new HEAD lands")
{
	GitWindow window("WorktreeHeadMovedRowsWindow", HeadOnMain(), {}, MainAndFeatureRead());
	window.WorktreePort.Listed = RepoLinkedAndLoose();
	window.Settle();

	ClickWorktreeRow(window, "loose");
	REQUIRE(window.Model.GetShownLogRequest().has_value());
	REQUIRE(window.Model.GetShownLogRequest()->From == kLooseHead);

	const int rowsBefore = window.HistoryRowCount();
	const std::vector<ImGuiID> idsBefore = HistoryRowIds(window);
	REQUIRE(rowsBefore >= 1);
	REQUIRE_FALSE(idsBefore.empty());

	// A commit made in the worktree, whose new page is still being read.
	const Core::Oid newHead = OidWithFirstByte(0x78);
	window.WorktreePort.Listed.back().Head.Commit = newHead;
	window.HoldsModelReads = true;
	ListWorktreesAgain(window);

	REQUIRE(window.Model.GetAskedLogRequest().has_value());
	REQUIRE(window.Model.GetAskedLogRequest()->From == newHead);
	REQUIRE(window.Model.GetShownLogRequest()->From == kLooseHead);

	// The commit's row stays where it was, rather than a wait standing in for it.
	CHECK(window.HistoryRowCount() == rowsBefore);
	CHECK(HistoryRowIds(window) == idsBefore);

	window.HoldsModelReads = false;
	window.HeldModelReads.RunAll();
	window.Settle();

	CHECK(window.Model.GetShownLogRequest()->From == newHead);
	CHECK(window.HistoryRowCount() == rowsBefore);
}
