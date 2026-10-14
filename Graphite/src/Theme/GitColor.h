#pragma once

#include <cstdint>

namespace Graphite {

	enum class GitColor : std::uint8_t
	{
		DiffAddedLine,
		DiffRemovedLine,

		DiffAddedText,
		DiffRemovedText,

		GutterAdded,
		GutterModified,
		GutterRemoved,

		GraphLane1,
		GraphLane2,
		GraphLane3,
		GraphLane4,
		GraphLane5,
		GraphLane6,

		Count
	};
}
