#include <doctest/doctest.h>

#include "DrawnColours.h"
#include "ImGuiFrames.h"

#include "Sessions/SessionDrawing.h"
#include "Theme/BuiltInSchemes.h"
#include "Theme/ChromePalette.h"

#include "Core/ImGui/Theme.h"

#include <imgui.h>

#include <array>

TEST_CASE("The chosen segment keeps the accent fill, though the theme gives a press no colour of its own")
{
	constexpr std::array<const char*, 2> kLabels{"Day", "Week"};
	constexpr ImVec2 kWindowSize(400.0f, 100.0f);

	for (const Graphite::ResolvedScheme* scheme : {&Graphite::DarkScheme(), &Graphite::LightScheme()})
	{
		ImGuiFrames frames;
		Core::Theme::Apply(Graphite::ChromePaletteFor(*scheme));

		int chosen = 1;
		bool drawsTheAccent = false;
		frames.Next([&] {
			ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
			ImGui::SetNextWindowSize(kWindowSize);
			ImGui::Begin("##segmentsTest", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoSavedSettings);
			Graphite::SegmentedChoice(kLabels, chosen);
			drawsTheAccent = DrawsInColour(*ImGui::GetWindowDrawList(), ImGui::GetColorU32(ImGuiCol_CheckMark));
			ImGui::End();
		});

		CHECK(drawsTheAccent);
	}
}
