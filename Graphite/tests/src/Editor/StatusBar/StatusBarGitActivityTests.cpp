#include <doctest/doctest.h>

#include "DrawnColours.h"
#include "ImGuiFrames.h"

#include "Editor/StatusBar/StatusBar.h"

#include "Core/ImGui/Animation.h"
#include "Core/ImGui/FontSet.h"
#include "Core/ImGui/UiScale.h"
#include "Core/Text/DocumentSet.h"

#include <imgui_internal.h>

#include <cstddef>
#include <optional>
#include <string>

namespace {

	constexpr int kFramesPastTheLoaderWait = 15;

	constexpr const char* kBranch = "main";

	float BarTextWidth(const char* text)
	{
		Core::PushInterfaceFont(Graphite::kStatusBarFontSize);
		const float width = ImGui::CalcTextSize(text).x;
		ImGui::PopFont();

		return width;
	}

	struct GitActivityScreen
	{
		ImGuiFrames Frames;
		Core::DocumentSet Documents;
		Graphite::StatusMessages Messages;
		Graphite::StatusBarClick LastClick = Graphite::StatusBarClick::None;

		void Draw()
		{
			Core::Animation::BeginFrame();
			const std::optional<std::size_t> noDocument;
			const Graphite::StatusBarClick clicked = Graphite::DrawStatusBar(Documents, noDocument, Messages);

			if (clicked != Graphite::StatusBarClick::None)
			{
				LastClick = clicked;
			}
		}

		void DrawFrames(int frameCount)
		{
			for (int frameIndex = 0; frameIndex < frameCount; ++frameIndex)
			{
				Frames.Next([this] { Draw(); });
			}
		}

		static const ImGuiWindow& Bar()
		{
			const ImGuiWindow* bar = ImGui::FindWindowByName(Graphite::kStatusBarWindowName);
			REQUIRE(bar != nullptr);
			return *bar;
		}

		static float ItemsLeft() { return Bar().Pos.x + Bar().WindowPadding.x; }

		static float BarMiddle() { return Bar().Pos.y + Bar().Size.y * 0.5f; }
	};
}

TEST_CASE("A running git write shows its words at once and its loader after a fifth of a second, frame after frame")
{
	GitActivityScreen screen;
	screen.Messages.Git = Graphite::GitStatusSegment{
		.Branch = kBranch,
		.HasRepository = true,
		.Activity = Graphite::GitActivity{.Text = "Staging 2 files", .Busy = true}};

	screen.DrawFrames(1);
	const int greyAtOnce = VerticesInColour(*GitActivityScreen::Bar().DrawList, Graphite::StatusBarTextColour());

	screen.DrawFrames(kFramesPastTheLoaderWait);
	const int greyOnceShown = VerticesInColour(*GitActivityScreen::Bar().DrawList, Graphite::StatusBarTextColour());

	CHECK(greyAtOnce > 0);
	CHECK(greyOnceShown > greyAtOnce);
}

TEST_CASE("An Explorer copy shows neither its words nor its loader for a fifth of a second, then both at once")
{
	GitActivityScreen screen;
	screen.DrawFrames(1);
	const int greyOfTheBarAlone = VerticesInColour(*GitActivityScreen::Bar().DrawList, Graphite::StatusBarTextColour());

	screen.Messages.ExplorerFileOperation = "Duplicating Foo\xE2\x80\xA6 12 files";

	screen.DrawFrames(1);
	const int greyAtOnce = VerticesInColour(*GitActivityScreen::Bar().DrawList, Graphite::StatusBarTextColour());

	screen.DrawFrames(kFramesPastTheLoaderWait);
	const int greyOnceShown = VerticesInColour(*GitActivityScreen::Bar().DrawList, Graphite::StatusBarTextColour());

	CHECK(greyAtOnce == greyOfTheBarAlone);
	CHECK(greyOnceShown > greyAtOnce);
}

TEST_CASE("A git write done is plain words beside the branch, with no loader")
{
	GitActivityScreen screen;
	screen.Messages.Git = Graphite::GitStatusSegment{
		.Branch = kBranch, .HasRepository = true, .Activity = Graphite::GitActivity{.Text = "Staged 2 files"}};

	screen.DrawFrames(1);
	const int greyAtOnce = VerticesInColour(*GitActivityScreen::Bar().DrawList, Graphite::StatusBarTextColour());

	screen.DrawFrames(kFramesPastTheLoaderWait);
	const int greyLater = VerticesInColour(*GitActivityScreen::Bar().DrawList, Graphite::StatusBarTextColour());

	CHECK(greyAtOnce > 0);
	CHECK(greyLater == greyAtOnce);
}

TEST_CASE("A question waiting in Git takes a click in the status bar and reports it as the Git panel's")
{
	constexpr const char* kWaiting = "Checkout of feature/x is waiting for your answer in Git";

	GitActivityScreen screen;
	screen.Messages.Git = Graphite::GitStatusSegment{
		.Branch = kBranch,
		.HasRepository = true,
		.Activity = Graphite::GitActivity{.Text = kWaiting, .OpensGit = true}};

	float activityMiddle = 0.0f;
	screen.Frames.Next([&screen, &activityMiddle] {
		screen.Draw();
		activityMiddle = GitActivityScreen::ItemsLeft() + BarTextWidth(kBranch) + Core::Scaled(Graphite::kStatusItemGap) +
						 BarTextWidth(kWaiting) * 0.5f;
	});

	screen.Frames.Click(ImVec2(activityMiddle, GitActivityScreen::BarMiddle()), [&screen] { screen.Draw(); });

	CHECK(screen.LastClick == Graphite::StatusBarClick::GitBranch);
}

TEST_CASE("A git write's words take no click")
{
	constexpr const char* kStaged = "Staged 2 files";

	GitActivityScreen screen;
	screen.Messages.Git = Graphite::GitStatusSegment{
		.Branch = kBranch, .HasRepository = true, .Activity = Graphite::GitActivity{.Text = kStaged}};

	float activityMiddle = 0.0f;
	screen.Frames.Next([&screen, &activityMiddle] {
		screen.Draw();
		activityMiddle = GitActivityScreen::ItemsLeft() + BarTextWidth(kBranch) + Core::Scaled(Graphite::kStatusItemGap) +
						 BarTextWidth(kStaged) * 0.5f;
	});

	screen.Frames.Click(ImVec2(activityMiddle, GitActivityScreen::BarMiddle()), [&screen] { screen.Draw(); });

	CHECK(screen.LastClick == Graphite::StatusBarClick::None);
}

TEST_CASE("A checkout named in place of the head turns a loader and still opens Git on a click")
{
	constexpr float kIntoTheItem = 30.0f;

	GitActivityScreen screen;
	screen.Messages.Git = Graphite::GitStatusSegment{
		.Branch = "Checking out feature/x", .BranchBusy = true, .HasRepository = true};

	screen.DrawFrames(kFramesPastTheLoaderWait);

	screen.Frames.Click(ImVec2(GitActivityScreen::ItemsLeft() + kIntoTheItem, GitActivityScreen::BarMiddle()),
						[&screen] { screen.Draw(); });

	CHECK(screen.LastClick == Graphite::StatusBarClick::GitBranch);
}
