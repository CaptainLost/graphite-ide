#pragma once

#include "Theme/GitPalette.h"

#include "Core/Git/Diff.h"
#include "Core/Git/Signature.h"

#include <chrono>
#include <cstddef>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>

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

	// The author as the list names them: the name git recorded, or the part of the email
	// before the '@' when there is no name, or "unknown" when there is neither — a commit
	// with no author is malformed but real, and a blank cell tells the reader nothing.
	std::string AuthorName(const Core::Signature& who);

	// The single-character ellipsis (U+2026) a cut cell ends in, as its UTF-8 bytes.
	inline constexpr std::string_view Ellipsis = "\xE2\x80\xA6";

	// How many bytes of `text` a cell `maxWidth` wide can show, when it cannot show all of
	// it: the longest prefix that still leaves `ellipsisWidth` for the ellipsis drawn after
	// it. Nothing when the whole text fits, which is the ordinary case and draws no ellipsis.
	//
	// Cut on a code point boundary, so a multi-byte character is kept or dropped whole, and
	// with trailing spaces dropped, so the ellipsis sits against the word it cuts short.
	//
	// `widthOf` measures a piece of text. It is handed in rather than asked of the font so
	// the rule can be checked with fixed widths; it must not shrink as the text grows,
	// which every font's measure satisfies.
	std::optional<std::size_t> CutToFit(std::string_view text, float maxWidth, float ellipsisWidth,
										const std::function<float(std::string_view)>& widthOf);

	// One letter for the badge beside a changed file: A/M/D/R/C/T. Unmodified never reaches
	// a diff's file list in practice, and gets a blank rather than a claim about a change
	// that did not happen.
	char ChangeLetter(Core::FileChange change);

	// What ChangeLetter's letter stands for, for the tooltip on it. Null for Unmodified, whose
	// blank has nothing to explain.
	const char* ChangeName(Core::FileChange change);

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
