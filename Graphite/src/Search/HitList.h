#pragma once

#include "Search/HitGroups.h"

#include <cstddef>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace Graphite {

	// WARNING: Keyed on the group's id and the hit's index, not on a pointer. Hits are appended
	// to groups as results arrive, and the vector holding them reallocates.
	struct HitSelection
	{
		std::string Group;
		std::size_t Hit = 0;
	};

	struct HitListInput
	{
		// WARNING: Not const, because OnGroupOpened lets a caller write into the group it is
		// handed.
		std::vector<HitGroup>& Groups;

		std::optional<HitSelection>& Selected;
		const std::function<void(const SearchHit&)>& OnOpen;
		const std::function<void(HitGroup&)>* OnGroupOpened = nullptr;
	};

	// WARNING: Call inside the child the results occupy. This does not open one.
	void DrawHitList(const HitListInput& input);
}
