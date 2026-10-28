#pragma once

#include "Core/ImGui/UiScale.h"

#include <imgui.h>
#include <imgui_internal.h>

#include <array>
#include <optional>
#include <string>

namespace Graphite {

	inline constexpr std::array<float, 17> kZoomSteps = {
		0.1f, 0.125f, 0.25f, 1.0f / 3.0f, 0.5f, 2.0f / 3.0f, 1.0f, 1.5f, 2.0f,
		3.0f, 4.0f, 6.0f, 8.0f, 12.0f, 16.0f, 24.0f, 32.0f};

	inline constexpr float kFitMargin = 24.0f;
	inline constexpr float kPanKeepVisible = 32.0f;
	inline constexpr float kNearestFromZoom = 1.0f;
	inline constexpr float kPixelGridFromZoom = 8.0f;

	// What the zoom shortcuts ask of the picture with the keyboard focus.
	enum class PictureZoom
	{
		In,
		Out,
		Fit
	};

	struct ZoomState
	{
		float Zoom = 1.0f;
		ImVec2 Pan{0.0f, 0.0f};
		bool Opened = false;
		// The part of a wheel notch not yet taken as a step.
		float WheelPending = 0.0f;
	};

	struct PixelPoint
	{
		int X = 0;
		int Y = 0;

		bool operator==(const PixelPoint&) const = default;
	};

	// The margins in screen pixels at the current interface scale. The functions below take them as arguments and know
	// no units, so what is scaled is decided where they are called.
	inline float FitMargin()
	{
		return Core::Scaled(kFitMargin);
	}

	inline float PanKeepVisible()
	{
		return Core::Scaled(kPanKeepVisible);
	}

	// The next step above `zoom`, or `zoom` itself at the top.
	float StepIn(float zoom);
	// The next step below `zoom`, or `zoom` itself when none is smaller.
	float StepOut(float zoom);

	// The largest zoom at which the whole picture fits the area less `margin` on each side.
	float FitZoom(ImVec2 picture, ImVec2 area, float margin);
	float OpeningZoom(ImVec2 picture, ImVec2 area, float margin);

	// Centred in the area, moved by the pan, corners rounded to whole pixels.
	ImRect PlacePicture(const ZoomState& state, ImVec2 picture, const ImRect& area);

	ZoomState ZoomAround(const ZoomState& state, float zoom, ImVec2 point, ImVec2 picture, const ImRect& area,
						 float keepVisible);
	ZoomState ClampPan(const ZoomState& state, ImVec2 picture, const ImRect& area, float keepVisible);

	// The file pixel under `point`, or nothing outside the picture.
	std::optional<PixelPoint> PixelUnder(ImVec2 point, const ZoomState& state, ImVec2 picture, const ImRect& area);

	std::string ZoomLabel(float zoom);

} // namespace Graphite
