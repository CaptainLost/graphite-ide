#include "Editor/TabColour.h"

namespace Graphite {

	std::optional<ImU32> TabDotColour(GitDecoration decoration, ImU32 statusColour)
	{
		if (decoration == GitDecoration::None)
		{
			return std::nullopt;
		}

		return statusColour;
	}
}
