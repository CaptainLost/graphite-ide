#include "Git/Tags/TagLookup.h"

#include "Core/Git/Ref.h"

namespace Graphite {

	bool IsTagName(std::string_view fullName)
	{
		return fullName.starts_with(Core::kTagPrefix);
	}

	std::optional<std::size_t> IndexOfTag(const std::vector<Core::Tag>& tags, std::string_view fullName)
	{
		for (std::size_t index = 0; index < tags.size(); ++index)
		{
			if (tags[index].FullName == fullName)
			{
				return index;
			}
		}

		return std::nullopt;
	}

	const Core::Tag* TagWithFullName(const std::vector<Core::Tag>& tags, std::string_view fullName)
	{
		const std::optional<std::size_t> index = IndexOfTag(tags, fullName);

		return index.has_value() ? &tags[*index] : nullptr;
	}
}
