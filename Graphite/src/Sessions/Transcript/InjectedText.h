#pragma once

#include <cstddef>
#include <string>
#include <string_view>

namespace Graphite {

	// Whether user-role text is something Claude Code wrote into the conversation — a
	// background task's notification, a local command's echo, a reminder — rather than words
	// the user typed. Recognised by a known opening at the start, after whitespace — a tag, or
	// the "[Request interrupted by user" line an interruption leaves: a prompt that only
	// mentions one, or starts with other markup, is still a prompt.
	bool IsInjectedText(std::string_view text);

	// What a compaction summary is labelled as. Its text opens with a sentence, not a tag, and
	// that sentence alone would not say the context was compacted there.
	inline constexpr std::string_view kCompactSummaryLabel = "Compaction summary";

	// One line saying what injected text was: a notification's own summary where it has one,
	// otherwise the kind of text it is.
	std::string InjectedLabel(std::string_view text);

	// One line saying what text with no known opening is: its first line, or for the line
	// Claude Code writes beside a pasted image, "[Image: source: <path>]", the image's file
	// name, since the path's head is what a cut would keep. At most `maxBytes` bytes; a label
	// that had to be cut ends in an ellipsis, counted within them.
	std::string FirstLineLabel(std::string_view text, std::size_t maxBytes);
}
