#include "corepch.h"

#include "Core/Platform/WindowPlacement.h"

#include "Core/Base/Base.h"

#include <algorithm>

namespace Core {

	namespace {
		// Enough of the window to take hold of: a strip the width of a couple of caption
		// buttons and the height of the caption.
		constexpr int kGrabbableWidth = 64;
		constexpr int kGrabbableHeight = 32;

		// The most of the work area a window with no saved placement takes.
		constexpr int kDefaultAreaPercent = 85;

		bool IsGrabbableOn(const WindowPlacement& placement, const ScreenRect& area)
		{
			const int left = std::max(placement.X, area.Left);
			const int right = std::min(placement.X + placement.Width, area.Right);
			const int top = std::max(placement.Y, area.Top);
			const int bottom = std::min(placement.Y + placement.Height, area.Bottom);

			return right - left >= kGrabbableWidth && bottom - top >= kGrabbableHeight;
		}
	}

	WindowPlacement DefaultPlacement(int width, int height, const ScreenRect& workArea)
	{
		const int areaWidth = workArea.Right - workArea.Left;
		const int areaHeight = workArea.Bottom - workArea.Top;

		WindowPlacement placement;
		placement.Width = std::min(width, areaWidth * kDefaultAreaPercent / 100);
		placement.Height = std::min(height, areaHeight * kDefaultAreaPercent / 100);
		placement.X = workArea.Left + (areaWidth - placement.Width) / 2;
		placement.Y = workArea.Top + (areaHeight - placement.Height) / 2;

		return placement;
	}

	WindowPlacement FitToMonitors(WindowPlacement placement, std::span<const ScreenRect> workAreas,
								  const ScreenRect& primary)
	{
		const bool grabbable =
			std::any_of(workAreas.begin(), workAreas.end(),
						[&placement](const ScreenRect& area) { return IsGrabbableOn(placement, area); });

		if (grabbable)
		{
			return placement;
		}

		const int areaWidth = primary.Right - primary.Left;
		const int areaHeight = primary.Bottom - primary.Top;

		placement.Width = std::min(placement.Width, areaWidth);
		placement.Height = std::min(placement.Height, areaHeight);
		placement.X = primary.Left + (areaWidth - placement.Width) / 2;
		placement.Y = primary.Top + (areaHeight - placement.Height) / 2;

		return placement;
	}
}
