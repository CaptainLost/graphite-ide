#include "corepch.h"

#include "Core/ImGui/LoadingState.h"

#include "Core/App/Redraw.h"

#include <imgui.h>
#include <imspinner_text.h>

#include <algorithm>

namespace Core {

	namespace {

		constexpr const char* kLoaderId = "##loadingState";

		constexpr float kSweepsPerSecond = 0.5f;

		float CentredStart(float space, float extent)
		{
			constexpr float kHalf = 0.5f;
			return std::max(0.0f, (space - extent) * kHalf);
		}
	}

	void LoadingState(const char* text)
	{
		const ImVec2 region = ImGui::GetContentRegionAvail();
		const float textWidth = ImGui::CalcTextSize(text).x;

		constexpr float kMinimumRegionWidth = 1.0f;
		constexpr float kRadiusPerDiameter = 0.5f;
		constexpr float kDiametersPerRadius = 2.0f;
		const float radius =
			std::min(textWidth, std::max(region.x, kMinimumRegionWidth)) * kRadiusPerDiameter;
		const float height =
			(radius + ImGui::GetStyle().FramePadding.y) * kDiametersPerRadius;

		const ImVec2 start = ImGui::GetCursorPos();
		ImGui::SetCursorPos(ImVec2(start.x + CentredStart(region.x, radius * kDiametersPerRadius),
								   start.y + CentredStart(region.y, height)));

		const ImColor ink{ImGui::GetStyleColorVec4(ImGuiCol_Text)};
		const ImColor paper{ImGui::GetStyleColorVec4(ImGuiCol_WindowBg)};
		ImSpinner::SpinnerTextShine(kLoaderId, radius, ink, paper, kSweepsPerSecond, text);

		Redraw::RequestIn(Redraw::Clock::duration::zero());
	}
}
