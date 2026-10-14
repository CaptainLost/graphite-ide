#pragma once

#include <cstddef>
#include <string>
#include <string_view>

namespace Graphite {

	bool IsInjectedText(std::string_view text);

	inline constexpr std::string_view kCompactSummaryLabel = "Compaction summary";

	std::string InjectedLabel(std::string_view text);

	std::string FirstLineLabel(std::string_view text, std::size_t maxBytes);
}
