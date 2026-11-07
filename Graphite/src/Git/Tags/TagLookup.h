#pragma once

#include "Core/Git/Tag.h"

#include <cstddef>
#include <optional>
#include <string_view>
#include <vector>

namespace Graphite {

	// The place in `tags` of the tag whose full name ("refs/tags/x") is `fullName`; empty when none is listed.
	std::optional<std::size_t> IndexOfTag(const std::vector<Core::Tag>& tags, std::string_view fullName);

	// The tag whose full name ("refs/tags/x") is `fullName`; null when none is listed.
	const Core::Tag* TagWithFullName(const std::vector<Core::Tag>& tags, std::string_view fullName);
}
