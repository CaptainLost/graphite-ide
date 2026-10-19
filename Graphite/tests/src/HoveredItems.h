#pragma once

#include "ImGuiFrames.h"

#include <imgui.h>
#include <imgui_internal.h>

#include <functional>
#include <vector>

struct HoveredStretch
{
	ImGuiID Id = 0;
	float From = 0.0f;
	float To = 0.0f;

	float Middle() const { return (From + To) * 0.5f; }
};

namespace HoveredItems {

	enum class Along
	{
		Across,
		Down
	};

	inline std::vector<HoveredStretch> Walk(ImGuiFrames& frames, Along along, float line, float from, float to,
											const std::function<void()>& draw)
	{
		constexpr float kStep = 1.0f;

		const ImGuiContext& context = *ImGui::GetCurrentContext();
		std::vector<HoveredStretch> stretches;

		for (float at = from; at <= to; at += kStep)
		{
			const ImVec2 point = along == Along::Across ? ImVec2(at, line) : ImVec2(line, at);
			ImGui::GetIO().AddMousePosEvent(point.x, point.y);
			frames.Next(draw);

			const ImGuiID hovered = context.HoveredId;

			if (hovered == 0)
			{
				continue;
			}

			if (!stretches.empty() && stretches.back().Id == hovered && stretches.back().To + kStep >= at)
			{
				stretches.back().To = at;
				continue;
			}

			stretches.push_back(HoveredStretch{.Id = hovered, .From = at, .To = at});
		}

		return stretches;
	}
}

inline std::vector<HoveredStretch> HoveredAcross(ImGuiFrames& frames, float y, float fromX, float toX,
												 const std::function<void()>& draw)
{
	return HoveredItems::Walk(frames, HoveredItems::Along::Across, y, fromX, toX, draw);
}

inline std::vector<HoveredStretch> HoveredDown(ImGuiFrames& frames, float x, float fromY, float toY,
											   const std::function<void()>& draw)
{
	return HoveredItems::Walk(frames, HoveredItems::Along::Down, x, fromY, toY, draw);
}
