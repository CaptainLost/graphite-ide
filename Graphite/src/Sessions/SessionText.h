#pragma once

#include "Sessions/Pricing.h"
#include "Sessions/Session.h"

#include <chrono>
#include <filesystem>
#include <optional>
#include <string>
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

	// Each turn's cost by bucket. A model with no price adds nothing here; the totals shown
	// beside the chart carry the "?".
	std::vector<BucketCosts> TurnCosts(const Session& session, const Pricing& pricing);
}
