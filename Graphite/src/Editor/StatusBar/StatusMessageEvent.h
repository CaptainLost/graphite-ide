#pragma once

#include <string>

namespace Graphite {

	// Something a panel wants the status bar to say for a few seconds - "Committed 1a2b3c4 Fix
	// it", "Copied path". Published on the application's event bus and shown through the same
	// transient slot the editor's own messages use, so a panel never reaches into
	// DocumentWindows for it and the bar still has one message at a time.
	struct StatusMessageEvent
	{
		std::string Text;
	};
}
