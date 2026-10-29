#include "Image/Zoom.h"

#include <algorithm>
#include <cmath>
#include <format>

namespace Graphite {

	namespace {

		constexpr float kStepTolerance = 1e-4f;

		ImVec2 AreaCentre(const ImRect& area)
		{
			return ImVec2((area.Min.x + area.Max.x) * 0.5f, (area.Min.y + area.Max.y) * 0.5f);
		}

		// Where the picture's top-left corner sits before rounding.
		ImVec2 ExactMin(const ZoomState& state, ImVec2 picture, const ImRect& area)
		{
			const ImVec2 centre = AreaCentre(area);
			return ImVec2(centre.x + state.Pan.x - picture.x * state.Zoom * 0.5f,
						  centre.y + state.Pan.y - picture.y * state.Zoom * 0.5f);
		}

		float ClampAxis(float pan, float size, float areaMin, float areaMax, float keepVisible)
		{
			const float centre = (areaMin + areaMax) * 0.5f;
			const float keep = std::min(keepVisible, size);
			const float low = areaMin + keep - size * 0.5f - centre;
			const float high = areaMax - keep + size * 0.5f - centre;
			return std::clamp(pan, low, std::max(low, high));
		}

	} // namespace

	float StepIn(float zoom)
	{
		for (const float step : kZoomSteps)
		{
			if (step > zoom * (1.0f + kStepTolerance))
			{
				return step;
			}
		}
		return zoom;
	}

	float StepOut(float zoom)
	{
		for (auto it = kZoomSteps.rbegin(); it != kZoomSteps.rend(); ++it)
		{
			if (*it < zoom * (1.0f - kStepTolerance))
			{
				return *it;
			}
		}
		return zoom;
	}

	float FitZoom(ImVec2 picture, ImVec2 area, float margin)
	{
		const float across = std::max(area.x - 2.0f * margin, 1.0f) / picture.x;
		const float down = std::max(area.y - 2.0f * margin, 1.0f) / picture.y;
		return std::min(across, down);
	}

	float OpeningZoom(ImVec2 picture, ImVec2 area, float margin)
	{
		const bool fits = picture.x <= area.x - 2.0f * margin && picture.y <= area.y - 2.0f * margin;
		return fits ? 1.0f : FitZoom(picture, area, margin);
	}

	ImRect PlacePicture(const ZoomState& state, ImVec2 picture, const ImRect& area)
	{
		const ImVec2 min = ExactMin(state, picture, area);
		const ImVec2 max(min.x + picture.x * state.Zoom, min.y + picture.y * state.Zoom);
		return ImRect(ImVec2(std::round(min.x), std::round(min.y)), ImVec2(std::round(max.x), std::round(max.y)));
	}

	ZoomState ClampPan(const ZoomState& state, ImVec2 picture, const ImRect& area, float keepVisible)
	{
		ZoomState result = state;
		result.Pan.x = ClampAxis(state.Pan.x, picture.x * state.Zoom, area.Min.x, area.Max.x, keepVisible);
		result.Pan.y = ClampAxis(state.Pan.y, picture.y * state.Zoom, area.Min.y, area.Max.y, keepVisible);
		return result;
	}

	ZoomState ZoomAround(const ZoomState& state, float zoom, ImVec2 point, ImVec2 picture, const ImRect& area,
						 float keepVisible)
	{
		const ImVec2 oldMin = ExactMin(state, picture, area);
		const ImVec2 inPicture((point.x - oldMin.x) / state.Zoom, (point.y - oldMin.y) / state.Zoom);

		const ImVec2 newMin(point.x - inPicture.x * zoom, point.y - inPicture.y * zoom);
		const ImVec2 centre = AreaCentre(area);

		ZoomState result = state;
		result.Zoom = zoom;
		result.Pan = ImVec2(newMin.x + picture.x * zoom * 0.5f - centre.x, newMin.y + picture.y * zoom * 0.5f - centre.y);
		return ClampPan(result, picture, area, keepVisible);
	}

	std::optional<PixelPoint> PixelUnder(ImVec2 point, const ZoomState& state, ImVec2 picture, const ImRect& area)
	{
		const ImRect placed = PlacePicture(state, picture, area);
		if (point.x < placed.Min.x || point.y < placed.Min.y || point.x >= placed.Max.x || point.y >= placed.Max.y)
		{
			return std::nullopt;
		}

		const float width = placed.Max.x - placed.Min.x;
		const float height = placed.Max.y - placed.Min.y;
		const int x = static_cast<int>(std::floor((point.x - placed.Min.x) / width * picture.x));
		const int y = static_cast<int>(std::floor((point.y - placed.Min.y) / height * picture.y));
		return PixelPoint{std::clamp(x, 0, static_cast<int>(picture.x) - 1),
						  std::clamp(y, 0, static_cast<int>(picture.y) - 1)};
	}

	std::string ZoomLabel(float zoom)
	{
		const double percent = static_cast<double>(zoom) * 100.0;
		const double doubled = percent * 2.0;
		const long long doubledWhole = std::llround(doubled);
		const bool isHalf = std::abs(doubled - static_cast<double>(doubledWhole)) < 1e-3 && doubledWhole % 2 != 0;
		if (isHalf)
		{
			return std::format("{:.1f}%", static_cast<double>(doubledWhole) / 2.0);
		}
		return std::format("{}%", std::llround(percent));
	}

} // namespace Graphite
