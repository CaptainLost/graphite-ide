#include "Editor/ActiveTextDocument.h"

namespace Graphite {

	std::optional<std::filesystem::path> ActiveTextDocument(const Core::DocumentSet& documents)
	{
		const std::optional<std::size_t> active = documents.GetActive();

		if (!active)
		{
			return std::nullopt;
		}

		const Core::OpenItem& item = documents.At(*active);

		if (!item.Document)
		{
			return std::nullopt;
		}

		return item.Path;
	}
}
