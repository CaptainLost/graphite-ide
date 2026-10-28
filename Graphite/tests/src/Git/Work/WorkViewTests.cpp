#include <doctest/doctest.h>

#include "ImGuiFrames.h"
#include "Git/Work/WorkView.h"
#include "Theme/ActiveScheme.h"

#include <imgui.h>
#include <imgui_internal.h>

#include <chrono>
#include <filesystem>
#include <optional>
#include <string>
#include <utility>

namespace {

	constexpr float kWindowWidth = 420.0f;
	constexpr float kWindowHeight = 500.0f;
	constexpr float kSweepStep = 10.0f;

	Graphite::WorkBoard BoardWithOneBranch()
	{
		Graphite::WorkBoard board;
		board.Trunk = "refs/heads/main";

		Graphite::WorkCard card;
		card.Kind = Graphite::WorkCardKind::Branch;
		card.Branch = "refs/heads/feature";
		card.Title = "Feature";
		card.TipSubject = "Add the feature";
		card.Ahead = 2;
		card.DistanceKnown = true;
		card.Checked = true;
		board.Cards.push_back(card);

		return board;
	}

	void DrawIn(Graphite::WorkView& view, const Graphite::WorkBoard& board, bool reading = false,
				const std::optional<Core::Error>& error = std::nullopt)
	{
		ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
		ImGui::SetNextWindowSize(ImVec2(kWindowWidth, kWindowHeight));
		ImGui::Begin("WorkViewTest", nullptr,
					 ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
						 ImGuiWindowFlags_NoSavedSettings);
		view.Draw(board, reading, error, std::chrono::sys_seconds{std::chrono::seconds{1700000000}});
		ImGui::End();
	}
}

TEST_CASE("The ready group starts collapsed and ExpandReady opens it")
{
	ImGuiFrames frames;
	const Graphite::ActiveScheme scheme;
	Graphite::WorkView view(scheme);
	const Graphite::WorkBoard board = BoardWithOneBranch();

	frames.Next([&] { DrawIn(view, board); });
	CHECK_FALSE(view.IsReadyExpanded());

	view.ExpandReady();
	frames.Next([&] { DrawIn(view, board); });
	CHECK(view.IsReadyExpanded());
}

TEST_CASE("The expanded ready group lays a list row out for each entry")
{
	ImGuiFrames frames;
	const Graphite::ActiveScheme scheme;
	Graphite::WorkView view(scheme);
	Graphite::WorkBoard board = BoardWithOneBranch();

	for (int entry = 0; entry < 3; ++entry)
	{
		board.Ready.push_back(Graphite::ReadyEntry{.Branch = "refs/heads/done" + std::to_string(entry),
												   .Worktree = std::filesystem::path("C:/wt/done" + std::to_string(entry)),
												   .TipTime = std::chrono::sys_seconds{std::chrono::seconds{1699990000}}});
	}

	std::optional<float> collapsedBottom;
	std::optional<float> expandedBottom;

	const auto draw = [&](std::optional<float>& bottom) {
		DrawIn(view, board);
		ImGui::Begin("WorkViewTest");
		bottom = ImGui::GetCurrentWindow()->DC.CursorMaxPos.y;
		ImGui::End();
	};

	frames.Next([&] { draw(collapsedBottom); });
	view.ExpandReady();
	frames.Next([&] { draw(expandedBottom); });
	frames.Next([&] { draw(expandedBottom); });

	REQUIRE(collapsedBottom.has_value());
	REQUIRE(expandedBottom.has_value());

	// The collapsed group's summary line gives way to the three rows.
	CHECK(*expandedBottom - *collapsedBottom >= 2.0f * ImGui::GetTextLineHeight());
}

TEST_CASE("A menu opened on a branch card still names that branch after the cards reorder")
{
	ImGuiFrames frames;
	const Graphite::ActiveScheme scheme;
	Graphite::WorkView view(scheme);
	Graphite::WorkBoard board = BoardWithOneBranch();
	Graphite::WorkCard other = board.Cards[0];
	other.Branch = "refs/heads/other";
	other.Title = "Other";
	board.Cards.push_back(other);

	std::optional<std::string> menuOf;
	view.DrawBranchMenu = [&](const std::string& branchFullName) { menuOf = branchFullName; };

	const auto draw = [&] { DrawIn(view, board); };
	frames.Next(draw);
	frames.Next(draw);

	for (float y = 5.0f; y < kWindowHeight && !menuOf.has_value(); y += kSweepStep)
	{
		frames.Click(ImVec2(50.0f, y), draw, ImGuiMouseButton_Right);
	}

	REQUIRE(menuOf == std::optional<std::string>{"refs/heads/feature"});

	std::swap(board.Cards[0], board.Cards[1]);
	menuOf.reset();
	frames.Next(draw);

	CHECK(menuOf == std::optional<std::string>{"refs/heads/feature"});
}

TEST_CASE("Review on a branch card opens its thread")
{
	ImGuiFrames frames;
	const Graphite::ActiveScheme scheme;
	Graphite::WorkView view(scheme);
	const Graphite::WorkBoard board = BoardWithOneBranch();

	std::optional<std::string> reviewed;
	view.OnReview = [&](const Graphite::WorkCard& card) { reviewed = card.Branch; };

	const auto draw = [&] { DrawIn(view, board); };
	frames.Next(draw);
	frames.Next(draw);

	// ImGui keeps no item's position past its frame, so the pointer sweeps the window until the button answers.
	for (float y = 5.0f; y < kWindowHeight && !reviewed.has_value(); y += kSweepStep)
	{
		for (float x = 5.0f; x < kWindowWidth && !reviewed.has_value(); x += kSweepStep)
		{
			frames.Click(ImVec2(x, y), draw);
		}
	}

	REQUIRE(reviewed.has_value());
	CHECK(*reviewed == "refs/heads/feature");
}
