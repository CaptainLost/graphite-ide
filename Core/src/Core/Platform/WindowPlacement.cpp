#include "corepch.h"

#include "Core/Platform/WindowPlacement.h"

#include "Core/Base/Base.h"

#include <algorithm>

namespace Core {

	namespace {
		constexpr int kGrabbableWidthOfTwoCaptionButtons = 64;
		constexpr int kGrabbableHeightOfCaption = 32;

		constexpr int kLargestDefaultPercentOfWorkArea = 85;
		constexpr int kHundredPercent = 100;

		int WidthOf(const ScreenRect& area)
		{
			return area.Right - area.Left;
		}

		int HeightOf(const ScreenRect& area)
		{
			return area.Bottom - area.Top;
		}

		int PercentOf(int length, int percent)
		{
			return length * percent / kHundredPercent;
		}

		void CentreIn(WindowPlacement& placement, const ScreenRect& area)
		{
			const int spareWidth = WidthOf(area) - placement.Width;
			const int spareHeight = HeightOf(area) - placement.Height;

			placement.X = area.Left + spareWidth / 2;
			placement.Y = area.Top + spareHeight / 2;
		}

		bool IsGrabbableOn(const WindowPlacement& placement, const ScreenRect& area)
		{
			const int overlapLeft = std::max(placement.X, area.Left);
			const int overlapRight = std::min(placement.X + placement.Width, area.Right);
			const int overlapTop = std::max(placement.Y, area.Top);
			const int overlapBottom = std::min(placement.Y + placement.Height, area.Bottom);

			return overlapRight - overlapLeft >= kGrabbableWidthOfTwoCaptionButtons && overlapBottom - overlapTop >= kGrabbableHeightOfCaption;
		}

		bool IsGrabbableOnAnyMonitor(const WindowPlacement& placement, std::span<const ScreenRect> workAreas)
		{
			for (const ScreenRect& workArea : workAreas)
			{
				if (IsGrabbableOn(placement, workArea))
				{
					return true;
				}
			}

			return false;
		}
	}

	WindowPlacement DefaultPlacement(int preferredWidth, int preferredHeight, const ScreenRect& workArea)
	{
		WindowPlacement placement{
			.Width = std::min(preferredWidth, PercentOf(WidthOf(workArea), kLargestDefaultPercentOfWorkArea)),
			.Height = std::min(preferredHeight, PercentOf(HeightOf(workArea), kLargestDefaultPercentOfWorkArea)),
		};
		CentreIn(placement, workArea);

		return placement;
	}

	WindowPlacement FitToMonitors(WindowPlacement placement, std::span<const ScreenRect> workAreas,
								  const ScreenRect& primaryWorkArea)
	{
		if (IsGrabbableOnAnyMonitor(placement, workAreas))
		{
			return placement;
		}

		placement.Width = std::min(placement.Width, WidthOf(primaryWorkArea));
		placement.Height = std::min(placement.Height, HeightOf(primaryWorkArea));
		CentreIn(placement, primaryWorkArea);

		return placement;
	}
}
