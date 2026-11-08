#pragma once

#include "Git/Compare/VsLabel.h"

#include <optional>
#include <string>
#include <string_view>

namespace Graphite {

	// The sentence above the history list when the shown reference needs one: merged, no commits of its own, an old copy
	// of another remote's default, history cut off, or no common history.
	std::optional<std::string> HistoryBannerFor(const VsInput& input);

	// The word of the action the banner offers, or none: a shallow clone's "History cut off here" offers
	// "Fetch full history".
	std::optional<std::string> HistoryBannerActionFor(const VsInput& input);

	// What the history header says of the shown reference's line: the vs label and the banner.
	struct HeaderWords
	{
		VsLabel Label;
		std::optional<std::string> Banner;
		std::optional<std::string> BannerAction; // set only with a Banner
	};

	struct DrawnHeaderWords
	{
		std::string ShownRef; // full name
		HeaderWords Words;
	};

	// The words drawn: `fresh`, but while they wait (HeaderWordsWait) for the same reference, the words drawn last.
	const HeaderWords& HeaderWordsToDraw(const HeaderWords& fresh, bool waits, std::string_view shownRef,
										 const std::optional<DrawnHeaderWords>& drawn);

	// The words wait while the distance they count is measured again, and while the check of the shown branch's contents
	// (`contentPending`: ContentCheckWaits) has not answered, so the label never shows "…" and widens back, nor
	// the banner rewraps, for a moment; and while the rows under them are still the page asked with the line before, so
	// the label, the banner row and the fold row change in the frame the page asked with the new line lands, rather than
	// the banner row going a moment before the fold row comes.
	bool HeaderWordsWait(bool measuring, bool rowsOfLineShown, bool contentPending);
}
