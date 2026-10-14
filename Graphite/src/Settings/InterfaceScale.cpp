#include "Settings/InterfaceScale.h"

#include <cstdlib>

namespace Graphite {

	int InterfaceScalePercentOrDefault(const std::string& stored)
	{
		constexpr int decimalBase = 10;
		char* parsedEnd = nullptr;
		const long parsedPercent = std::strtol(stored.c_str(), &parsedEnd, decimalBase);
		const bool parsedNothing = parsedEnd == stored.c_str();

		if (parsedNothing || parsedPercent <= 0)
		{
			return kDefaultInterfaceScalePercent;
		}

		int nearestPercent = kInterfaceScalePercents.front();

		for (const int offeredPercent : kInterfaceScalePercents)
		{
			if (std::labs(offeredPercent - parsedPercent) < std::labs(nearestPercent - parsedPercent))
			{
				nearestPercent = offeredPercent;
			}
		}

		return nearestPercent;
	}
}
