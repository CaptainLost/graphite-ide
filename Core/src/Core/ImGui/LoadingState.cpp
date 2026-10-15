#include "corepch.h"

#include "Core/ImGui/LoadingState.h"

#include "Core/App/Redraw.h"

#include <imgui.h>
#include <imspinner.h>
#include <imspinner_text.h>

#include <algorithm>

namespace Core {

	namespace {

		constexpr const char* kLoaderId = "##loadingState";

		constexpr float kSweepsPerSecond = 0.5f;

		constexpr const char* kLineLoaderId = "##loadingLine";

		constexpr float kSmallestSpinnerRadius = 2.0f;
		constexpr float kLineSpinnerThickness = 2.0f;
		constexpr float kLineSpinnerSpeed = 2.8f;
		constexpr float kLineSpinnerArc = 3.14159265f;

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

	void LoadingLine(const char* text)
	{
		constexpr float kRadiusPerDiameter = 0.5f;

		// WARNING: An imspinner spinner pads its circle by the frame padding above and below, so the
		// radius leaves that padding out or the row grows taller than a line of text.
		const float framePadding = ImGui::GetStyle().FramePadding.y;
		const float radius =
			std::max(kSmallestSpinnerRadius, ImGui::GetTextLineHeight() * kRadiusPerDiameter - framePadding);

		const ImVec2 rowStart = ImGui::GetCursorScreenPos();

		const ImColor ink{ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled)};
		const ImColor track{ImGui::GetStyleColorVec4(ImGuiCol_FrameBg)};
		ImSpinner::SpinnerAng(kLineLoaderId, radius, kLineSpinnerThickness, ink, track, kLineSpinnerSpeed, kLineSpinnerArc);

		// WARNING: The text is placed by position rather than SameLine: SameLine would carry the
		// spinner's frame-padding baseline over and draw the text that much lower than the spinner.
		const float spinnerRight = ImGui::GetItemRectMax().x;
		ImGui::SetCursorScreenPos(ImVec2(spinnerRight + ImGui::GetStyle().ItemSpacing.x, rowStart.y));
		ImGui::TextDisabled("%s", text);

		Redraw::RequestIn(Redraw::Clock::duration::zero());
	}
}
