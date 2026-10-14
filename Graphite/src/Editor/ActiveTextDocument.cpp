#include "Editor/ActiveTextDocument.h"

namespace Graphite {

	std::optional<std::filesystem::path> ActiveTextDocument(const Core::DocumentSet& documents)
	{
		const std::optional<std::size_t> activeIndex = documents.GetActiveIndex();

		if (!activeIndex)
		{
			return std::nullopt;
		}

		const Core::OpenItem& item = documents.At(*activeIndex);

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
		const std::optional<std::size_t> shownIndex = ShownActiveTab(documents, isShown);

		if (!shownIndex || !documents.At(*shownIndex).Document)
		{
			return std::nullopt;
		}

		return shownIndex;
	}

	std::optional<std::size_t> ShownActiveTab(
		const Core::DocumentSet& documents,
		const std::function<bool(const std::filesystem::path&)>& isShown)
	{
		const std::optional<std::size_t> activeIndex = documents.GetActiveIndex();

		if (!activeIndex || !isShown(documents.At(*activeIndex).Path))
		{
			return std::nullopt;
		}

		return activeIndex;
	}
}
