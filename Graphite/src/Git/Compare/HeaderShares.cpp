#include "Git/Compare/HeaderShares.h"

#include <algorithm>

namespace Graphite {

	namespace {

		// The items after the name: the vs button when there is one, the filter fields and the switch.
		float GapsAfterName(const HeaderWidths& widths)
		{
			const float items = widths.VsButton > 0.0f ? 3.0f : 2.0f;
			return items * widths.Spacing;
		}
	}

	HeaderShares ShareHeader(const HeaderWidths& widths)
	{
		HeaderShares shares;
		shares.VsButton = std::max(0.0f, std::min(widths.Row * kVsButtonShare, widths.VsButton));

		const float shared =
			std::max(0.0f, widths.Row - widths.Taken - shares.VsButton - widths.Switch - GapsAfterName(widths));

		const float titleBasis = widths.Operation + widths.Prefix + widths.Title;
		const float wanted = titleBasis + widths.Fields + widths.Spacing;
		const float titleRoom = wanted > shared ? titleBasis * shared / wanted : titleBasis;

		shares.NameRoom = std::max(0.0f, titleRoom - widths.Operation);

		return shares;
	}

	FilterFieldWidths SplitFilterFields(float room, float fullPathWidth, float fieldsBasis, float spacing)
	{
		const float available = std::max(0.0f, room);

		FilterFieldWidths widths;
		widths.Path = std::min(fullPathWidth, available * fullPathWidth / fieldsBasis);
		widths.Words = std::max(0.0f, available - widths.Path - spacing);

		return widths;
	}
}
