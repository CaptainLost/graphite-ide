#include "corepch.h"

#include "Core/ImGui/LoadingState.h"

#include "Core/App/Redraw.h"

#include <imgui.h>
#include <imspinner_text.h>

#include <algorithm>

namespace Core {

	namespace {

		constexpr const char* kLoaderId = "##loadingState";

		// One sweep of the band across the text every two seconds.
		constexpr float kSweepsPerSecond = 0.5f;

		// A loader bigger than the space it is centred in starts at the space's edge: centred
		// regardless, it would begin above or left of the window, where no scrolling reaches it.
		float CentredStart(float space, float extent)
		{
			return std::max(0.0f, (space - extent) * 0.5f);
		}
	}

	void LoadingState(const char* text)
	{
		const ImVec2 region = ImGui::GetContentRegionAvail();
		const float textWidth = ImGui::CalcTextSize(text).x;

		// The loader is as wide as twice its radius and shrinks its text to fit that width, so
		// the radius is what keeps a long text inside a narrow window.
		const float radius = std::min(textWidth, std::max(region.x, 1.0f)) * 0.5f;
		const float height = (radius + ImGui::GetStyle().FramePadding.y) * 2.0f;

		const ImVec2 start = ImGui::GetCursorPos();
		ImGui::SetCursorPos(ImVec2(start.x + CentredStart(region.x, radius * 2.0f),
								   start.y + CentredStart(region.y, height)));

		const ImColor ink{ImGui::GetStyleColorVec4(ImGuiCol_Text)};
		const ImColor paper{ImGui::GetStyleColorVec4(ImGuiCol_WindowBg)};
		ImSpinner::SpinnerTextShine(kLoaderId, radius, ink, paper, kSweepsPerSecond, text);

		Redraw::RequestIn(Redraw::Clock::duration::zero());
	}
}
