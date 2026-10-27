#pragma once

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
	};

	struct PixelPoint
	{
		int X = 0;
		int Y = 0;

		bool operator==(const PixelPoint&) const = default;
	};

	// The next step above `zoom`, or `zoom` itself at the top.
	float StepIn(float zoom);
	// The next step below `zoom`, or `zoom` itself when none is smaller.
	float StepOut(float zoom);

	// The largest zoom at which the whole picture fits the area less kFitMargin on each side.
	float FitZoom(ImVec2 picture, ImVec2 area);
	float OpeningZoom(ImVec2 picture, ImVec2 area);

	// Centred in the area, moved by the pan, corners rounded to whole pixels.
	ImRect PlacePicture(const ZoomState& state, ImVec2 picture, const ImRect& area);

	ZoomState ZoomAround(const ZoomState& state, float zoom, ImVec2 point, ImVec2 picture, const ImRect& area);
	ZoomState ClampPan(const ZoomState& state, ImVec2 picture, const ImRect& area);

	// The file pixel under `point`, or nothing outside the picture.
	std::optional<PixelPoint> PixelUnder(ImVec2 point, const ZoomState& state, ImVec2 picture, const ImRect& area);

	std::string ZoomLabel(float zoom);

} // namespace Graphite
