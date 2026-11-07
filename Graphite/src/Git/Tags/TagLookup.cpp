#include "Git/Tags/TagLookup.h"

namespace Graphite {

	const Core::Tag* TagWithFullName(const std::vector<Core::Tag>& tags, std::string_view fullName)
	{
		for (const Core::Tag& tag : tags)
		{
			if (tag.FullName == fullName)
			{
				return &tag;
			}
		}

		return nullptr;
	}
}
