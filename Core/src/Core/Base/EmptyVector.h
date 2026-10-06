#pragma once

#include <vector>

namespace Core {

	// A function-local static rather than a namespace-scope vector, which would be built during
	// static initialisation in no defined order relative to its users in other files.
	template<typename Element>
	const std::vector<Element>& SharedEmptyVector()
	{
		static const std::vector<Element> empty;

		return empty;
	}
}
