#pragma once

#include "Sessions/Pricing.h"
#include "Sessions/Requests/RequestTimeline.h"
#include "Sessions/Session.h"
#include "Sessions/SessionSummary.h"

#include <chrono>
#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace Graphite {

	// U+25CF BLACK CIRCLE: a transcript written to within the live window.
	inline constexpr std::string_view kLiveSessionMark = "\xE2\x97\x8F";

	// "?" whenever part of the cost is unknown: a partial sum shown as a total would be a
	// wrong number that looks right.
	std::string FormatCost(const CostResult& cost);

	// Why a cost reads "?" - the models with no price, and where to add one - or nothing when
	// the cost is known. For the tooltip over the cost.
	std::string UnknownCostHint(const CostResult& cost);

	// A session list shows a session unless it is empty (see SessionSummary::IsEmpty); one live or
	// open in a tab is shown regardless, since it is about to have a first prompt.
	bool ShowsInSessionList(const SessionSummary& summary, bool live);

	// "Reading transcripts... 12 / 40", or without the count while the total is not known.
	std::string ScanProgressText(std::size_t done, std::size_t total);

	// What an empty session list says, by why it is empty: still reading, a text filter that
	// matches nothing, the workspace-only filter, or simply no sessions.
	std::string EmptyListMessage(bool scanning, std::size_t done, std::size_t total, bool workspaceOnly,
								 std::string_view filter);

	std::string FormatDuration(std::chrono::milliseconds span);

	// A compaction on one line: "Context compacted", then whichever of these it knows — the
	// token counts before and after (only with both), the trigger, the duration — and `time`
	// last when it is not empty, joined by " · ".
	std::string CompactionLabel(const CompactionFacts& facts, const std::string& time);

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

	// One of a turn's requests, as the Cost tab lists them under the turn.
	struct TurnRequest
	{
		// requestId, else uuid: the key the timeline knows it by.
		std::string Key;

		// Its position in the timeline, for the Requests tab to show it; nothing when the
		// timeline has no request by that key.
		std::optional<std::size_t> Position;

		CostResult Cost;
	};

	struct ToolCount
	{
		std::string Name;
		std::size_t Count = 0;
	};

	// Where a turn's money went: its requests, the dearest first and any with no price last,
	// since its cost cannot be placed among the others; and the tools it called, the most used
	// first and by name among equals.
	struct TurnBreakdown
	{
		std::vector<TurnRequest> Requests;
		std::vector<ToolCount> Tools;
	};

	// The turn at `turn` in `session` broken down, or nothing for a turn it does not have.
	TurnBreakdown BreakDownTurn(const Session& session, std::size_t turn, const RequestTimeline& timeline,
								const Pricing& pricing);

	// What a request did, a line each: every tool it called, as "Read src/a.cpp", or when it
	// called none, the start of its reply in quotes. Nothing for a request that did neither.
	std::vector<std::string> RequestActivity(const ApiRequest& request);

	// `text` as an ImGui label: on one line, with every run of whitespace a single space, and
	// with no "##", which ImGui would take as the start of the widget's id and hide the rest
	// behind. At most `maxBytes`, cut on a character.
	std::string SingleLineLabel(std::string_view text, std::size_t maxBytes);
}
