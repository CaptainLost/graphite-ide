#include <doctest/doctest.h>

#include "GitFixtures.h"
#include "ImGuiFrames.h"
#include "TempAppSettings.h"
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
#include "Git/Sidebar/SidebarView.h"
#include "Git/Window/WindowBar.h"
#include "Git/Worktrees/WorktreePort.h"
#include "Git/Worktrees/WorktreesModel.h"
#include "Theme/ActiveScheme.h"
#include "UI/Dialogs/ModalGate.h"
#include "UI/Lists/ListRow.h"
#include "Workspace/Workspace.h"

#include "Core/Events/EventBus.h"
#include "Core/Git/Lines/AncestryGraph.h"
#include "Core/Git/Tag.h"

#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace {

	using Graphite::Fixtures::LineReadBuilder;

	constexpr int kTopicCount = 30;
	constexpr ImVec2 kWindowSize(1200.0f, 700.0f);

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

	// The repository as git answers it: the read's branches, the HEAD and the tags given, no remotes. A history is one
	// commit, the tip of the reference it is asked for.
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

		Core::Result<Core::LogPage> QueryLog(const Core::LogRequest& request) override
		{
			Core::LogPage page;
			page.Complete = true;

			if (const std::optional<Core::Oid> tip = TipOf(request.Ref))
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

	class NoWorktreesPort final : public Graphite::WorktreePort
	{
	public:
		Core::Result<std::vector<Core::WorktreeEntry>> QueryWorktrees() override { return std::vector<Core::WorktreeEntry>{}; }

		Core::Result<std::size_t> CountUncommitted(const std::filesystem::path&) override { return std::size_t{0}; }
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

	// The Git window drawn alone, its models fed by the ports above and answered at once.
	struct GitWindow
	{
		explicit GitWindow(const char* windowName, Core::HeadState head = HeadOnMain(), std::vector<Core::Tag> tags = {})
			: WindowName(windowName)
			, Head(std::move(head))
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

			Model.OnUpdate(noTime);
			Lines.Follow(Model.GetBranches(), Model.GetBranchListingGeneration(), Model.GetTags());
			Lines.OnUpdate(noTime);
			Worktrees.Follow(Model.GetBranches(), Model.GetWorkingDirectory());
			Worktrees.OnUpdate(noTime);
			View.OnUpdate(noTime);

			Frames.Next([this] {
				ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
				ImGui::SetNextWindowSize(kWindowSize);
				ImGui::Begin(WindowName, nullptr, ImGuiWindowFlags_NoSavedSettings);
				BarTop = ImGui::GetCursorScreenPos().y - ImGui::GetStyle().WindowPadding.y;
				View.OnImGuiRender();
				ChipId = ImGui::GetID("##headChip");
				ImGui::End();
			});

			View.SetDrawnOnLatestFrame(true);
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
			constexpr float kStep = 4.0f;
			const float middle = BarTop + Graphite::WindowBarEm(36.0f) * 0.5f;

			for (float x = 0.0f; x < kWindowSize.x; x += kStep)
			{
				ImGui::GetIO().AddMousePosEvent(x, middle);
				Step();

				if (ImGui::GetCurrentContext()->HoveredId == ChipId)
				{
					return true;
				}
			}

			return false;
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

		// The repository changed under the window, as a fetch changes it, and the model's refresh has run.
		void Refresh()
		{
			Events.Publish(Graphite::RepositoryChangedEvent{});
			Model.OnUpdate(Core::Timestep{Graphite::Debouncer::kMaxSeconds});
			Settle();
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

		Graphite::LineRead Read = MakeRead(Head);
		FixedHeadPort Port;
		OneReadLinesPort LinesPort{Read};
		NoWorktreesPort WorktreePort;
		NoContentPort ContentPort;

		Graphite::GitModel Model{Port, RunJobsInline()};
		Graphite::LinesModel Lines{LinesPort, RunJobsInline(), [] { return LineReadBuilder::Now().time_since_epoch().count(); }};
		Graphite::WorktreesModel Worktrees{WorktreePort, RunJobsInline()};
		Graphite::ContentModel Content{ContentPort, RunJobsInline()};

		Graphite::ModalGate Modals;
		Core::EventBus Events;
		FakeEditorBuffers Buffers;
		Graphite::ActiveScheme Scheme;
		Graphite::ChangesShared Shared;
		Graphite::ChangesView Changes{Model, Modals, Buffers, Scheme, Events, Shared, "window"};
		TempAppSettings Settings;
		Graphite::EditorSettings EditorPreferences{Settings.Get()};
		Graphite::DiffWindows Diffs;

		Graphite::GitView View{Model,  Lines,   Worktrees, Content, Changes, Scheme, EditorPreferences,
							   Diffs, Modals, Buffers,   Events};

		float BarTop = 0.0f;
		ImGuiID ChipId = 0;
	};

	// Yours lists main, feature and the topics, each two lines high, under its one-line header.
	constexpr int kBranchCount = 2 + kTopicCount;

	// Whether `middle` lies in the row that starts `top` down the sidebar's list and is `height` high.
	bool InSlot(std::optional<float> middle, float top, float height)
	{
		return middle.has_value() && *middle > top && *middle < top + height;
	}

	// The slot of the branch row at `place` in Yours, 0 for its first row.
	float YoursRowTop(int place)
	{
		return Graphite::ListRowPitch() + static_cast<float>(place) * Graphite::SidebarTwoLineRowHeight();
	}
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

	ImGuiWindow* rows = window.SidebarRows();
	REQUIRE(rows != nullptr);
	ImGui::SetScrollY(rows, rows->ScrollMax.y);
	window.Step();

	// The Tags section comes closed, after Yours; opened, it lists the tag's row under its header.
	const float pitch = Graphite::ListRowPitch();
	const float tagsTop = YoursRowTop(kBranchCount);
	window.ClickAt(window.InSidebarRows(tagsTop + pitch * 0.5f));
	window.ClickAt(window.InSidebarRows(tagsTop + pitch * 1.5f));

	CHECK(window.View.CaptureWindowState().Ref == "refs/tags/v1");
	CHECK(InSlot(window.SelectedRowMiddle(), tagsTop + pitch, pitch));

	REQUIRE(window.Model.GetShownLogRequest().has_value());
	CHECK(window.Model.GetShownLogRequest()->Ref == std::optional<std::string>{"refs/tags/v1"});
}
