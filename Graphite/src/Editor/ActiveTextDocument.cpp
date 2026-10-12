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
		const std::optional<std::size_t> shown = ShownActiveTab(documents, isShown);

		if (!shown || !documents.At(*shown).Document)
		{
			return std::nullopt;
		}

		return shown;
	}

	std::optional<std::size_t> ShownActiveTab(
		const Core::DocumentSet& documents,
		const std::function<bool(const std::filesystem::path&)>& isShown)
	{
		const std::optional<std::size_t> active = documents.GetActiveIndex();

		if (!active || !isShown(documents.At(*active).Path))
		{
			return std::nullopt;
		}

		return active;
	}
}
