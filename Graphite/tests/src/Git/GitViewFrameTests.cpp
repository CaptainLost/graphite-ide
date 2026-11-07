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
#include "Git/Diff/DiffWindows.h"
#include "Git/GitView.h"
#include "Git/Lines/LinesModel.h"
#include "Git/Lines/LinesPort.h"
#include "Git/Window/WindowBar.h"
#include "Git/Worktrees/WorktreePort.h"
#include "Git/Worktrees/WorktreesModel.h"
#include "Theme/ActiveScheme.h"
#include "UI/Dialogs/ModalGate.h"

#include "Core/Events/EventBus.h"

#include <imgui.h>
#include <imgui_internal.h>

#include <cmath>
#include <cstdint>
#include <cstring>
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

		return builder.Build();
	}

	// The repository as git answers it: the read's branches, the HEAD given, no remotes.
	class FixedHeadPort final : public EmptyGitPort
	{
	public:
		FixedHeadPort(std::vector<Core::Branch> branches, Core::HeadState head)
			: m_Branches(std::move(branches))
			, m_Head(std::move(head))
		{
		}

		Core::Result<std::vector<Core::Branch>> QueryBranches() override { return m_Branches; }

		Core::Result<Core::HeadState> QueryHead() override { return m_Head; }

	private:
		std::vector<Core::Branch> m_Branches;
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
		explicit GitWindow(const char* windowName, Core::HeadState head = HeadOnMain())
			: WindowName(windowName)
			, Head(std::move(head))
		{
			Model.SetRoot("C:/repo");
			Model.RequestStatus();
			Model.RequestBranches();
			Model.RequestHead();
			Model.RequestStashes();
		}

		void Step()
		{
			const Core::Timestep noTime{0.0f};

			Model.OnUpdate(noTime);
			Lines.Follow(Model.GetBranches(), Model.GetBranchListingGeneration());
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
		FixedHeadPort Port{*Read.Branches, Head};
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
}

TEST_CASE("Git window: clicking the head chip shows HEAD's history with its row selected and in view in that frame")
{
	GitWindow window("HeadChipWindow");
	window.Settle();

	window.View.RestoreWindowState(Graphite::GitWindowState{.Ref = "refs/heads/feature"});
	window.Settle();
	REQUIRE(window.View.CaptureWindowState().Ref == "refs/heads/feature");

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
