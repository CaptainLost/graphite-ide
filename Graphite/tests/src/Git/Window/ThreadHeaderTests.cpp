#include <doctest/doctest.h>

#include "ImGuiFrames.h"
#include "Git/Window/ThreadHeader.h"

#include <imgui.h>

#include <optional>

namespace {

	constexpr float kWindowWidth = 1000.0f;
	constexpr float kWindowHeight = 300.0f;
	constexpr float kLeftColumnWidth = 300.0f;
	constexpr float kSweepStep = 6.0f;
	constexpr float kHalf = 0.5f;
}

TEST_CASE("Open in own tab sits inside the main column of the Git window and answers a click there")
{
	ImGuiFrames frames;
	bool clicked = false;

	const Graphite::ThreadHeaderInput input{.Card = nullptr, .TrunkName = "main", .Branch = "refs/heads/topic"};

	// As the Git window draws it: straight into the second column of a table, not inside a child.
	const auto draw = [&] {
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
			clicked = Graphite::DrawThreadHeader(input) || clicked;

			ImGui::EndTable();
		}

		ImGui::End();
	};

	frames.Next(draw);
	frames.Next(draw);

	const float titleRow = ImGui::GetStyle().WindowPadding.y + ImGui::GetFrameHeight() * kHalf;
	std::optional<float> answeredAt;

	// ImGui keeps no item's position past its frame, so the pointer sweeps the title row until the button answers.
	for (float x = kWindowWidth - kSweepStep; x > 0.0f && !answeredAt.has_value(); x -= kSweepStep)
	{
		frames.Click(ImVec2(x, titleRow), draw);

		if (clicked)
		{
			answeredAt = x;
		}
	}

	REQUIRE(answeredAt.has_value());
	CHECK(*answeredAt > kLeftColumnWidth);
	CHECK(*answeredAt < kWindowWidth);
}
