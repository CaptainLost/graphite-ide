#pragma once

#include <string>
#include <string_view>

namespace Core {

	class EventBus;

}

namespace Graphite {

	// The message of the toast that confirms a copy, the same for every Copy action: "path:
	// D:\Repos\a.cpp" under the title "Copied". `what` names the thing copied; the text itself
	// follows, its first line only and cut to a line's worth on a character boundary, so a
	// commit message or an error copied whole does not flood the toast.
	std::string CopiedSummary(std::string_view what, std::string_view text);

	// Puts `text` on the clipboard and confirms it with a Success toast; see ReportCopied.
	void CopyToClipboard(Core::EventBus& events, std::string_view what, const std::string& text);

	// Only the confirmation, for text something else has already put on the clipboard - a
	// selection's menu Copy, say - so the clipboard is not set a second time. A short Success
	// toast, published as a ToastEvent, rather than the status bar's transient slot, which is
	// kept for the routine outcome of a save or a commit (ADR 0557). Plain Ctrl+C is never
	// confirmed: the key is pressed without looking, and its callers do not call this.
	void ReportCopied(Core::EventBus& events, std::string_view what, std::string_view text);
}
