#pragma once

#include "Git/Compare/VsLabel.h"

#include <optional>
#include <string>
#include <string_view>

namespace Graphite {

	// The sentence above the history list when the shown reference needs one: merged, no commits of its own, an old copy
	// of another remote's default, history cut off, or no common history.
	std::optional<std::string> HistoryBannerFor(const VsInput& input);

	// What the history header says of the shown reference's line: the vs label and the banner.
	struct HeaderWords
	{
		VsLabel Label;
		std::optional<std::string> Banner;
	};

	struct DrawnHeaderWords
	{
		std::string ShownRef; // full name
		HeaderWords Words;
	};

	// The words drawn: `fresh`, but while the distance they count is measured again for the same reference, the words
	// drawn last, so the label never shows "…" and widens back, nor the banner rewraps, for a moment.
	const HeaderWords& HeaderWordsToDraw(const HeaderWords& fresh, bool measuring, std::string_view shownRef,
										 const std::optional<DrawnHeaderWords>& drawn);
}
