#pragma once

#include <string>
#include <string_view>

namespace Core {

	class EventBus;

}

namespace Graphite {

	// What the status bar says after a copy, the same for every Copy action: "Copied path:
	// D:\Repos\a.cpp". `what` names the thing copied; the text itself follows, its first line
	// only and cut to a status bar's worth on a character boundary, so a commit message or an
	// error copied whole does not flood the bar.
	std::string CopiedMessage(std::string_view what, std::string_view text);

	// Puts `text` on the clipboard and confirms it in the status bar with CopiedMessage.
	void CopyToClipboard(Core::EventBus& events, std::string_view what, const std::string& text);

	// Only the confirmation, for text something else has already put on the clipboard — a
	// selection gesture's Ctrl+C, say — so the clipboard is not set a second time.
	void ReportCopied(Core::EventBus& events, std::string_view what, std::string_view text);
}
