#include <doctest/doctest.h>

#include "ImGuiFrames.h"

#include "UI/Lists/ListRow.h"

#include "Core/ImGui/FontSet.h"

#include <imgui.h>

TEST_CASE("Rows keep their pitch: a list row's pitch does not follow the frame padding that sizes fields")
{
	ImGuiFrames frames;

	frames.Next([] {
		ImGui::PushFont(nullptr, Core::kInterfaceFontSize);
		ImGuiStyle& style = ImGui::GetStyle();

		style.FramePadding.y = 4.0f;
		const float pitchBesideShortFields = Graphite::ListRowPitch();
		const float textTopBesideShortFields = Graphite::ListRowTextTop(0.0f);

		style.FramePadding.y = 9.0f;
		CHECK(Graphite::ListRowPitch() == doctest::Approx(pitchBesideShortFields));
		CHECK(Graphite::ListRowTextTop(0.0f) == doctest::Approx(textTopBesideShortFields));

		ImGui::PopFont();
	});
}
