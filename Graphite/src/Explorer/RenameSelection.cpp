#include "Explorer/RenameSelection.h"

namespace Graphite {

	RenameSpan RenameSelection(std::string_view name, bool isFolder)
	{
		if (!isFolder)
		{
			const std::size_t lastDot = name.rfind('.');
			const bool hasExtension = lastDot != std::string_view::npos && lastDot > 0;

			if (hasExtension)
			{
				return RenameSpan{.Start = 0, .End = lastDot};
			}
		}

		return RenameSpan{.Start = 0, .End = name.size()};
	}
}
