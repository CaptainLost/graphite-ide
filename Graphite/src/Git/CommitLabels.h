#pragma once

#include "Git/GitPalette.h"

#include "Core/Git/Diff.h"
#include "Core/Git/Signature.h"

#include <chrono>
#include <cstddef>
#include <filesystem>
#include <optional>
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

	// The author's name in exactly `width` display columns: padded with spaces when
	// short, and cut with a single-character ellipsis when long, so every summary in the
	// list starts at the same column and the list reads as a table rather than as ragged
	// text. Width is code points, not bytes, so a multi-byte UTF-8 name still lines up —
	// except for a code point a font renders double-wide (CJK, most emoji), which still
	// counts as one column here and will shift what follows it; that case is out of scope.
	//
	// Falls back to the part of the email before the '@' when there is no name, and to
	// "unknown" when there is neither — a commit with no author is malformed but real,
	// and a blank column tells the reader nothing.
	std::string PaddedAuthor(const Core::Signature& who, std::size_t width);

	// RelativeAge in exactly `width` display columns, padded or cut the same way.
	std::string PaddedAge(std::chrono::sys_seconds when, std::chrono::sys_seconds now,
						  std::size_t width);

	// One letter for the badge beside a changed file: A/M/D/R/C/T. Unmodified never reaches
	// a diff's file list in practice, and gets a blank rather than a claim about a change
	// that did not happen.
	char ChangeLetter(Core::FileChange change);

	// The colour that letter is drawn in: the gutter colours the editor already marks
	// changes with, so a file added in a commit and a line added in the working tree read
	// as the same event. A rename, a copy and a type change all leave the file where it was
	// with something about it different, which is what the modified colour says.
	//
	// Zero for a file the commit left alone, whose letter is a space — colouring nothing
	// would tint the row's spacing with a colour that means nothing.
	ImU32 ChangeColor(Core::FileChange change, const GitPalette& palette);

	// The path a changed file is shown under. A deleted file has no new path, so this falls
	// back to the old one — a deletion is exactly the change a reader most wants to find by
	// name.
	//
	// Not DisplayPath: that name is already a struct in Graphite::Search (HitDisplay.h), and
	// a translation unit that ever includes both would have the function hide the type.
	const std::filesystem::path& ShownPath(const Core::FileDiff& file);

	// Which file in a diff carries this path, on either side — a deleted file has no new
	// path, which is why both sides are checked rather than only NewPath, the same rule
	// ShownPath above states for the same reason. Matching by path rather than by index is
	// what lets a selection survive a refresh that reorders the list.
	//
	// Lives here rather than beside either of its two callers (GitView, which shows a
	// selected file's diff, and DiffWindows, which shows one reopened as a tab) so that
	// neither has to depend on the other to reach it.
	std::optional<std::size_t> IndexOfFile(const Core::Diff& diff,
										   const std::filesystem::path& path);
}
