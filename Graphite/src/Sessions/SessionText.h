#pragma once

#include "Sessions/Pricing.h"
#include "Sessions/Session.h"

#include <chrono>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace Graphite {

	// "?" whenever part of the cost is unknown: a partial sum shown as a total would be a
	// wrong number that looks right.
	std::string FormatCost(const CostResult& cost);

	std::string FormatDuration(std::chrono::milliseconds span);

	// The one field of a tool's input a reader scans for — the command, the path, the
	// pattern, the subagent's task — or the start of the raw input for a tool not known here.
	std::string ToolSummary(const ToolCall& tool);

	// For Edit, its old and new text as "- " and "+ " lines; for anything else, nothing.
	std::vector<std::string> EditLines(const ToolCall& tool);

	// The file a Read, Edit or Write acted on.
	std::optional<std::filesystem::path> ToolFilePath(const ToolCall& tool);

	struct TurnCost
	{
		// The priced part of the turn, by bucket. A model with no price adds nothing here.
		BucketCosts Buckets;

		// False when some of the turn's tokens went to a model with no price: Buckets is then
		// part of the cost, and a reader must be shown "?" rather than it.
		bool Known = true;
	};

	// Each turn's cost. A chart may draw the priced part of a turn that is not Known; a
	// number shown as the turn's cost may not.
	std::vector<TurnCost> TurnCosts(const Session& session, const Pricing& pricing);

	// `text` as an ImGui label: on one line, with every run of whitespace a single space, and
	// with no "##", which ImGui would take as the start of the widget's id and hide the rest
	// behind. At most `maxBytes`, cut on a character.
	std::string SingleLineLabel(std::string_view text, std::size_t maxBytes);
}
