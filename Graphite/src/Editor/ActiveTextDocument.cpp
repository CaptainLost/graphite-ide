#include "Editor/ActiveTextDocument.h"

namespace Graphite {

	std::optional<std::filesystem::path> ActiveTextDocument(const Core::DocumentSet& documents)
	{
		const std::optional<std::size_t> active = documents.GetActiveIndex();

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

	std::optional<std::size_t> ShownActiveDocument(
		const Core::DocumentSet& documents,
		const std::function<bool(const std::filesystem::path&)>& isShown)
	{
		const std::optional<std::size_t> active = documents.GetActiveIndex();

		if (!active)
		{
			return std::nullopt;
		}

		const Core::OpenItem& item = documents.At(*active);

		if (!item.Document || !isShown(item.Path))
		{
			return std::nullopt;
		}

		return active;
	}
}
