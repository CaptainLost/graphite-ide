#include <doctest/doctest.h>

#include "ImGuiFrames.h"
#include "Git/Window/ChipRow.h"
#include "Theme/BuiltInSchemes.h"

#include <imgui.h>
#include <imgui_internal.h>

#include <cstddef>
#include <string>
#include <vector>

namespace {

	constexpr float kSpacing = 4.0f;
	constexpr float kPlusWidth = 20.0f;

	float PlusWidth(std::size_t)
	{
		return kPlusWidth;
	}
}

TEST_CASE("Every chip is shown when they fit in the lines, or when the lines are not limited")
{
	const std::vector<float> widths = {60.0f, 60.0f, 60.0f};

	CHECK(Graphite::ChipsShown(widths, 200.0f, kSpacing, 1, PlusWidth) == 3);
	CHECK(Graphite::ChipsShown(widths, 100.0f, kSpacing, 0, PlusWidth) == 3);
	CHECK(Graphite::ChipsShown(widths, 100.0f, kSpacing, 3, PlusWidth) == 3);
}

TEST_CASE("Chips past the last line give way to a chip that counts them, on the last line")
{
	// Two to a line of 130: 60 + 4 + 60. Five need three lines.
	const std::vector<float> widths = {60.0f, 60.0f, 60.0f, 60.0f, 60.0f};

	// The second line holds the third chip and the "+N" chip, which takes the fourth one's place.
	CHECK(Graphite::ChipsShown(widths, 130.0f, kSpacing, 2, PlusWidth) == 3);

	// One line: the first chip and the count.
	CHECK(Graphite::ChipsShown(widths, 130.0f, kSpacing, 1, PlusWidth) == 1);
}

TEST_CASE("A chip row cut to two lines draws no third line")
{
	ImGuiFrames frames;
	const Graphite::ChipColours colours = Graphite::ReadChipColours(Graphite::DarkScheme());

	std::vector<Graphite::Chip> chips;

	for (int index = 0; index < 8; ++index)
	{
		chips.push_back({"chip number " + std::to_string(index), Graphite::ChipTone::Info});
	}

	float top = 0.0f;
	float bottom = 0.0f;

	frames.Next([&] {
		ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
		ImGui::SetNextWindowSize(ImVec2(260.0f, 300.0f));
		ImGui::Begin("Chips", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings);
		top = ImGui::GetCursorScreenPos().y;
		Graphite::DrawChipRow(chips, colours, 2);
		bottom = ImGui::GetCurrentWindow()->DC.CursorMaxPos.y;
		ImGui::End();
	});

	CHECK(bottom - top <= 2.0f * Graphite::ChipHeight() + ImGui::GetStyle().ItemSpacing.y + 1.0f);
}
