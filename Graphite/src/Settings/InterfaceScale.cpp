#include "Settings/InterfaceScale.h"

#include <cstdlib>

namespace Graphite {

	int InterfaceScalePercentOrDefault(const std::string& stored)
	{
		char* end = nullptr;
		const long parsed = std::strtol(stored.c_str(), &end, 10);

		// strtol leaves end at the start of the string when it parsed nothing.
		if (end == stored.c_str() || parsed <= 0)
		{
			return kDefaultInterfaceScalePercent;
		}

		int nearest = kInterfaceScalePercents.front();

		for (const int percent : kInterfaceScalePercents)
		{
			if (std::labs(percent - parsed) < std::labs(nearest - parsed))
			{
				nearest = percent;
			}
		}

		return nearest;
	}
}
