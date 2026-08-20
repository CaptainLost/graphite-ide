#pragma once

#include <cstdint>

namespace Graphite {

	// Colours nothing else in the application needs. Kept apart from EditorColor because a
	// scheme author reading a settings panel should find every git colour in one group, and
	// apart from ChromeColor because these carry meaning — added, removed, one branch versus
	// another — rather than describing a surface.
	enum class GitColor : std::uint8_t
	{
		// The band behind a whole added or removed line in a diff.
		DiffAddedLine,
		DiffRemovedLine,

		// The text on those lines. Separate from the band so a scheme can use a strong
		// background with quiet text, or the reverse, without one washing the other out.
		DiffAddedText,
		DiffRemovedText,

		// The marks beside a line number in the editor, drawn against HEAD. Defined here
		// before anything draws them: a scheme file is a user-facing format, and adding
		// half the git colours now and half later leaves a hand-written file silently
		// incomplete twice.
		GutterAdded,
		GutterModified,
		GutterRemoved,

		// One per lane in the commit graph, reused cyclically past the sixth. Six because
		// a history with seven branches visible at once is rare enough that repeating a
		// colour there costs less than asking a scheme author to pick twelve.
		GraphLane1,
		GraphLane2,
		GraphLane3,
		GraphLane4,
		GraphLane5,
		GraphLane6,

		Count
	};
}
