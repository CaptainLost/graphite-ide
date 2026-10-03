#include "Explorer/RenameSelection.h"

namespace Graphite {

	RenameSpan RenameSelection(std::string_view name, bool isFolder)
	{
		if (!isFolder)
		{
			const std::size_t lastDot = name.rfind('.');

			if (lastDot != std::string_view::npos && lastDot > 0)
			{
				return {0, lastDot};
			}
		}

		return {0, name.size()};
	}
}
