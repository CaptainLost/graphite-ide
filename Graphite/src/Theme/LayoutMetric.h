#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace Graphite {

	// The measurements a scheme sets besides its colours, in unscaled pixels.
	enum class LayoutMetric : std::uint8_t
	{
		PanelGap,
		PanelRounding,
		ControlRounding,
		BorderWidth,
		ScrollbarWidth,
		Count
	};

	constexpr std::size_t kLayoutMetricCount = static_cast<std::size_t>(LayoutMetric::Count);

	struct LayoutRange
	{
		float Smallest;
		float Largest;
	};

	// The measurements every built-in scheme starts from; a scheme that sets none looks as the interface always did.
	constexpr std::array<float, kLayoutMetricCount> DefaultLayout()
	{
		return {1.0f, 4.0f, 6.0f, 1.0f, 12.0f};
	}

	constexpr LayoutRange RangeOf(LayoutMetric metric)
	{
		switch (metric)
		{
		case LayoutMetric::PanelGap:
			return {0.0f, 12.0f};
		case LayoutMetric::PanelRounding:
			return {0.0f, 14.0f};
		case LayoutMetric::ControlRounding:
			return {0.0f, 10.0f};
		case LayoutMetric::BorderWidth:
			return {0.0f, 3.0f};
		case LayoutMetric::ScrollbarWidth:
			return {6.0f, 20.0f};
		case LayoutMetric::Count:
			break;
		}

		return {0.0f, 0.0f};
	}
}
