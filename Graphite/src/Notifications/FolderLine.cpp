#include "Notifications/FolderLine.h"

#include <algorithm>
#include <cstddef>
#include <vector>

namespace Graphite {

	namespace {
		constexpr std::string_view kEllipsis = "\u2026";

		bool StartsCharacter(char byte)
		{
			const unsigned char value = static_cast<unsigned char>(byte);
			const unsigned char continuationMask = 0xC0u;
			const unsigned char continuationBits = 0x80u;
			return (value & continuationMask) != continuationBits;
		}

		std::vector<std::size_t> TailStartsLongestFirst(std::string_view folder)
		{
			std::vector<std::size_t> tailStarts;
			for (std::size_t byteIndex = 1; byteIndex < folder.size(); ++byteIndex)
			{
				if (StartsCharacter(folder[byteIndex]))
				{
					tailStarts.push_back(byteIndex);
				}
			}
			tailStarts.push_back(folder.size());
			return tailStarts;
		}

		std::string Elided(std::string_view folder, std::size_t tailStart)
		{
			return std::string(kEllipsis) + std::string(folder.substr(tailStart));
		}

		bool ElidedIsTooWide(std::string_view folder, std::size_t tailStart, float width, const TextWidthMeasure& measure)
		{
			return measure(Elided(folder, tailStart)) > width;
		}
	}

	std::string ElideFolderLine(std::string_view folder, float width, const TextWidthMeasure& measure)
	{
		const bool fitsWhole = measure(folder) <= width;
		if (fitsWhole)
		{
			return std::string(folder);
		}

		const std::vector<std::size_t> tailStarts = TailStartsLongestFirst(folder);
		const std::vector<std::size_t>::const_iterator longestFittingTail = std::partition_point(
			tailStarts.begin(), tailStarts.end(),
			[&](std::size_t tailStart) { return ElidedIsTooWide(folder, tailStart, width, measure); });

		const bool evenTheEllipsisIsTooWide = longestFittingTail == tailStarts.end();
		if (evenTheEllipsisIsTooWide)
		{
			return std::string(kEllipsis);
		}

		return Elided(folder, *longestFittingTail);
	}
}
