#pragma once

#include <string>
#include <string_view>

namespace Core {

	class EventBus;

}

namespace Graphite {

	std::string CopiedSummary(std::string_view copiedThing, std::string_view text);

	void CopyToClipboard(Core::EventBus& events, std::string_view copiedThing, const std::string& text);

	void ReportCopied(Core::EventBus& events, std::string_view copiedThing, std::string_view text);
}
