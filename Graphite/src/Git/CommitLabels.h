#pragma once

#include "Core/Git/Diff.h"
#include "Core/Git/Signature.h"

#include <chrono>
#include <cstddef>
#include <filesystem>
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

	// The author's name in exactly `width` characters: padded with spaces when short,
	// and cut with a single-character ellipsis when long, so every summary in the list
	// starts at the same column and the list reads as a table rather than as ragged text.
	//
	// Falls back to the part of the email before the '@' when there is no name, and to
	// "unknown" when there is neither — a commit with no author is malformed but real,
	// and a blank column tells the reader nothing.
	std::string PaddedAuthor(const Core::Signature& who, std::size_t width);

	// RelativeAge in exactly `width` characters, padded or cut the same way.
	std::string PaddedAge(std::chrono::sys_seconds when, std::chrono::sys_seconds now,
						  std::size_t width);

	// One letter for the badge beside a changed file: A/M/D/R/C/T. Unmodified never reaches
	// a diff's file list in practice, and gets a blank rather than a claim about a change
	// that did not happen.
	char ChangeLetter(Core::FileChange change);

	// The path a changed file is shown under. A deleted file has no new path, so this falls
	// back to the old one — a deletion is exactly the change a reader most wants to find by
	// name.
	//
	// Not DisplayPath: that name is already a struct in Graphite::Search (HitDisplay.h), and
	// a translation unit that ever includes both would have the function hide the type.
	const std::filesystem::path& ShownPath(const Core::FileDiff& file);
}
