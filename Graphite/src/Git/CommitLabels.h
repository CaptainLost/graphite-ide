#pragma once

#include "Core/Git/Signature.h"

#include <chrono>
#include <string>

namespace Graphite {

	// "3 hours ago". Takes the current time rather than reading a clock, so the result is a
	// pure function of its arguments and can be checked against fixed instants.
	//
	// A commit dated in the future — ordinary, when two machines' clocks disagree — reads
	// as "just now" rather than as a negative age.
	std::string RelativeAge(std::chrono::sys_seconds when, std::chrono::sys_seconds now);

	// One or two letters for the badge beside a commit. Falls back to the email's first
	// letter when there is no name, and to "?" when there is neither.
	std::string AuthorInitials(const Core::Signature& who);
}
