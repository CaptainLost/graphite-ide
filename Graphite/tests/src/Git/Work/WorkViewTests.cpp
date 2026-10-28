#include <doctest/doctest.h>

#include "ImGuiFrames.h"
#include "Git/Work/WorkView.h"

#include <imgui.h>

#include <chrono>
#include <optional>
#include <string>

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
	Graphite::WorkView view;
	const Graphite::WorkBoard board = BoardWithOneBranch();

	frames.Next([&] { DrawIn(view, board); });
	CHECK_FALSE(view.IsReadyExpanded());

	view.ExpandReady();
	frames.Next([&] { DrawIn(view, board); });
	CHECK(view.IsReadyExpanded());
}

TEST_CASE("Review on a branch card opens its thread")
{
	ImGuiFrames frames;
	Graphite::WorkView view;
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
