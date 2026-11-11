#pragma once

#include "Sessions/Pricing.h"

#include "Core/Base/Result.h"

#include <string_view>

namespace Graphite {

	// The markdown form of Anthropic's pricing page, which the documentation serves for programs to read.
	inline constexpr std::string_view kPricingPageUrl = "https://platform.claude.com/docs/en/about-claude/pricing.md";

	// The table under "Model pricing", one price per model id: "Claude Opus 5.5" is claude-opus-5-5, a note in
	// brackets after the name is dropped, and of two rows for one model, such as a dearer tier for long prompts, the
	// first is kept. WARNING: The page is written for people and its layout is no contract. A missing section, columns
	// other than the five prices in their order, a price that does not read as dollars per million tokens or a table
	// with no model is an error, never an empty or partial list, so a changed page cannot quietly price anything wrong.
	Core::Result<PriceList> ReadPricingPage(std::string_view markdown);
}
