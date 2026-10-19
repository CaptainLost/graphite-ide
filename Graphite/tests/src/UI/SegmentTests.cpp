#include <doctest/doctest.h>

#include "DrawnColours.h"
#include "ImGuiFrames.h"

#include "UI/Segment.h"

#include "Core/ImGui/Theme.h"

#include <imgui.h>
#include <imgui_internal.h>

#include <functional>

namespace {
	const ImVec2 kFarAway(700.0f, 600.0f);

	struct SegmentScreen
	{
		SegmentScreen() { Core::Theme::Apply(Core::Theme::DefaultPalette()); }

		void Draw(const std::function<void()>& segment)
		{
			ImGui::GetIO().AddMousePosEvent(kFarAway.x, kFarAway.y);

			Frames.Next([&] {
				ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
				ImGui::SetNextWindowSize(ImVec2(300.0f, 200.0f));
				ImGui::Begin("Segments", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings);
				segment();
				ImGui::End();
			});
		}

		static const ImDrawList& DrawList()
		{
			const ImGuiWindow* window = ImGui::FindWindowByName("Segments");
			REQUIRE(window != nullptr);
			return *window->DrawList;
		}

		ImGuiFrames Frames;
	};
}

TEST_CASE("Segments of either height are clear at rest when not chosen, as every text button is")
{
	for (const bool small : {false, true})
	{
		CAPTURE(small);
		SegmentScreen screen;
		screen.Draw([small] {
			if (small)
			{
				Graphite::DrawSmallSegment("Day", false);
			}
			else
			{
				Graphite::DrawSegment("Day", false);
			}
		});

		CHECK_FALSE(DrawsInColour(SegmentScreen::DrawList(), ImGui::GetColorU32(Core::SurfaceColour())));
		CHECK_FALSE(DrawsInColour(SegmentScreen::DrawList(), ImGui::GetColorU32(ImGuiCol_CheckMark)));
	}
}

TEST_CASE("The chosen segment of either height is filled with the accent")
{
	for (const bool small : {false, true})
	{
		CAPTURE(small);
		SegmentScreen screen;
		screen.Draw([small] {
			if (small)
			{
				Graphite::DrawSmallSegment("Day", true);
			}
			else
			{
				Graphite::DrawSegment("Day", true);
			}
		});

		CHECK(DrawsInColour(SegmentScreen::DrawList(), ImGui::GetColorU32(ImGuiCol_CheckMark)));
	}
}
