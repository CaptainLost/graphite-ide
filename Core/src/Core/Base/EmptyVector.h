#pragma once

#include <vector>

namespace Core {

	// Function-local, not a global object (ADR 0033).
	template<typename Element>
	const std::vector<Element>& SharedEmptyVector()
	{
		static const std::vector<Element> empty;

		return empty;
	}
}
