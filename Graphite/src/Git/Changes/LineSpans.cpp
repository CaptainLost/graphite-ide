#include "Git/Changes/LineSpans.h"

#include "Core/Text/Encoding.h"

#include <algorithm>

namespace Graphite {

	ChangedSpan RemovedSpan(std::string_view oldLine, std::string_view newLine)
	{
		const std::size_t sharedLength = std::min(oldLine.size(), newLine.size());

		std::size_t prefixLength = 0;

		while (prefixLength < sharedLength && oldLine[prefixLength] == newLine[prefixLength])
		{
			++prefixLength;
		}

		prefixLength = Core::CharacterBoundaryAtOrBefore(oldLine, prefixLength);

		const std::size_t suffixRoom = sharedLength - std::min(prefixLength, sharedLength);
		std::size_t suffixLength = 0;

		while (suffixLength < suffixRoom &&
			   oldLine[oldLine.size() - 1 - suffixLength] == newLine[newLine.size() - 1 - suffixLength])
		{
			++suffixLength;
		}

		std::size_t removedEnd = oldLine.size() - suffixLength;

		removedEnd = Core::CharacterBoundaryAtOrAfter(oldLine, removedEnd);

		return {prefixLength, std::max(prefixLength, removedEnd)};
	}
}
