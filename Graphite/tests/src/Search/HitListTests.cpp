#include <doctest/doctest.h>

#include "Search/HitList.h"

#include "ImGuiFrames.h"

#include <imgui.h>
#include <imgui_internal.h>

#include <functional>
#include <optional>
#include <vector>

using namespace Graphite;

TEST_CASE("The hit list draws two frames in the code face and leaves the font stack balanced")
{
	SearchHit hit;
	hit.File = "a.cpp";
	hit.Line = 4;
	hit.Text = "int value = 1;";
	hit.From = 4;
	hit.To = 9;

	HitGroup group;
	group.Id = "a.cpp";
	group.Name = "a.cpp";
	group.File = "a.cpp";
	group.Hits.push_back(hit);

	std::vector<HitGroup> groups{group};
	std::optional<HitSelection> selected;
	const std::function<void(const SearchHit&)> onOpen = [](const SearchHit&) {};

	ImGuiFrames frames;

	for (int frame = 0; frame < 2; ++frame)
	{
		frames.Next([&] {
			ImGui::Begin("hits");
			ImGui::SetNextItemOpen(true);
			DrawHitList({.Groups = groups, .Selected = selected, .OnOpen = onOpen});
			ImGui::End();
		});
	}

	CHECK(ImGui::GetCurrentContext()->FontStack.Size == 0);
}
