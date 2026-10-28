#include <doctest/doctest.h>

#include "ImGuiFrames.h"
#include "Git/Window/ThreadHeader.h"

#include <imgui.h>

#include <algorithm>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace {

	constexpr float kWindowWidth = 1000.0f;
	constexpr float kWindowHeight = 300.0f;
	constexpr float kLeftColumnWidth = 300.0f;
	constexpr float kSweepStep = 6.0f;
	constexpr float kHalf = 0.5f;

	// As the Git window draws it: straight into the second column of a table, not inside a child.
	void DrawInMainColumn(const Graphite::ThreadHeaderInput& input, Graphite::ThreadHeaderClick& clicked)
	{
		ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
		ImGui::SetNextWindowSize(ImVec2(kWindowWidth, kWindowHeight));
		ImGui::Begin("frame", nullptr, ImGuiWindowFlags_NoDecoration);

		if (ImGui::BeginTable("columns", 2))
		{
			ImGui::TableSetupColumn("navigator", ImGuiTableColumnFlags_WidthFixed, kLeftColumnWidth);
			ImGui::TableSetupColumn("main", ImGuiTableColumnFlags_WidthStretch);
			ImGui::TableNextRow();

			ImGui::TableSetColumnIndex(0);
			ImGui::TextUnformatted("navigator");

			ImGui::TableSetColumnIndex(1);

			if (const Graphite::ThreadHeaderClick click = Graphite::DrawThreadHeader(input);
				click != Graphite::ThreadHeaderClick::None)
			{
				clicked = click;
			}

			ImGui::EndTable();
		}

		ImGui::End();
	}

	// ImGui keeps no item's position past its frame, so the pointer sweeps the title row until a button answers.
	std::optional<float> FirstAnswer(ImGuiFrames& frames, const std::function<void()>& draw,
									 const Graphite::ThreadHeaderClick& clicked, float from, float step)
	{
		const float titleRow = ImGui::GetStyle().WindowPadding.y + ImGui::GetFrameHeight() * kHalf;

		for (float x = from; x > 0.0f && x < kWindowWidth; x += step)
		{
			frames.Click(ImVec2(x, titleRow), draw);

			if (clicked != Graphite::ThreadHeaderClick::None)
			{
				return x;
			}
		}

		return std::nullopt;
	}
}

TEST_CASE("Open in own tab sits inside the main column of the Git window and answers a click there")
{
	ImGuiFrames frames;
	Graphite::ThreadHeaderClick clicked = Graphite::ThreadHeaderClick::None;

	const Graphite::ThreadHeaderInput input{.Card = nullptr, .TrunkName = "main", .Branch = "refs/heads/topic"};
	const std::function<void()> draw = [&] { DrawInMainColumn(input, clicked); };

	frames.Next(draw);
	frames.Next(draw);

	const std::optional<float> answeredAt = FirstAnswer(frames, draw, clicked, kWindowWidth - kSweepStep, -kSweepStep);

	REQUIRE(answeredAt.has_value());
	CHECK(clicked == Graphite::ThreadHeaderClick::OpenInOwnTab);
	CHECK(*answeredAt > kLeftColumnWidth);
	CHECK(*answeredAt < kWindowWidth);
}

TEST_CASE("The thread header counts the viewed files, and shows no count that will not come")
{
	Graphite::ThreadHeaderInput input{.Card = nullptr, .TrunkName = "main", .Branch = "refs/heads/topic"};
	const auto hasChip = [](const std::vector<Graphite::Chip>& chips, const std::string& text) {
		return std::ranges::find(chips, text, &Graphite::Chip::Text) != chips.end();
	};

	CHECK(hasChip(Graphite::ThreadHeaderChips(input), "counting viewed files\xE2\x80\xA6"));

	input.ReviewProblem = "topic has no common ancestor with main.";

	CHECK_FALSE(hasChip(Graphite::ThreadHeaderChips(input), "counting viewed files\xE2\x80\xA6"));
}

TEST_CASE("Back to Work leads the thread header at the main column's left edge")
{
	ImGuiFrames frames;
	Graphite::ThreadHeaderClick clicked = Graphite::ThreadHeaderClick::None;

	const Graphite::ThreadHeaderInput input{.Card = nullptr, .TrunkName = "main", .Branch = "refs/heads/topic"};
	const std::function<void()> draw = [&] { DrawInMainColumn(input, clicked); };

	frames.Next(draw);
	frames.Next(draw);

	const std::optional<float> answeredAt = FirstAnswer(frames, draw, clicked, kLeftColumnWidth, kSweepStep);

	REQUIRE(answeredAt.has_value());
	CHECK(clicked == Graphite::ThreadHeaderClick::BackToWork);
	CHECK(*answeredAt < kLeftColumnWidth + 2.0f * ImGui::GetFrameHeight());
}
