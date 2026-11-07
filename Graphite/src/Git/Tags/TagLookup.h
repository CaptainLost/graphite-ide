#pragma once

#include "Core/Git/Tag.h"

#include <string_view>
#include <vector>

namespace Graphite {

	// The tag whose full name ("refs/tags/x") is `fullName`; null when none is listed.
	const Core::Tag* TagWithFullName(const std::vector<Core::Tag>& tags, std::string_view fullName);
}
