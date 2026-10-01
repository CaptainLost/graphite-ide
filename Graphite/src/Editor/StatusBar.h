#pragma once

#include "Git/GitStatusText.h"

#include <filesystem>
#include <optional>
#include <string>

namespace Core {

	class DocumentSet;

}

namespace Graphite {

	// Bundled rather than two adjacent std::optional<std::string> parameters: a caller could
	// transpose those silently, which is exactly the hazard ADR 0035 exists to prevent.
	struct StatusMessages
	{
		std::optional<std::string> Transient;
		std::optional<std::string> Search;

		// Joins the struct rather than becoming a fourth parameter, for the same reason the
		// comment above gives: a caller could transpose two adjacent std::optional<std::string>
		// parameters silently, which is exactly the hazard ADR 0035 exists to prevent.
		//
		// Holds Graphite::ServerStatusText's output — empty when the active document's language
		// has no server configured, in which case nothing is drawn for it at all.
		std::optional<std::string> Server;

		// The longer text behind Server, shown as a tooltip when the server phrase is hovered —
		// Graphite::ServerStatusDetail's output for the same status. A second field rather than
		// folded into Server itself: the bar has room for the short phrase only, and encoding two
		// strings into one to save a field would just move the transposition hazard the comment
		// above already guards against into string parsing instead.
		std::optional<std::string> ServerDetail;

		// Preformatted from DiagnosticsModel::Total(), the same way Server is preformatted from a
		// ServerStatus rather than handed over raw — StatusBar.cpp stays free of DiagnosticsModel
		// entirely. Unset, not a zeroed Counts, when both counts are zero: an empty slot is the
		// resting state, and a "0 errors" nobody asked for would be the one thing in this bar that
		// has to be read to learn there is nothing to read.
		std::optional<std::string> Problems;

		// Graphite::BackgroundAnalysisText's output — what the pass reading the workspace behind
		// the reader has got through. Its own field rather than folded into Server: that slot
		// speaks for the active document's own server and has a priority order between three
		// different things to say, none of which this is.
		std::optional<std::string> Background;

		// The longer text behind Background. Joins the single tooltip the bar already hangs off
		// its whole right-hand side, because splitting that into separately hoverable items is a
		// change to how the bar draws for the sake of a second tooltip.
		std::optional<std::string> BackgroundDetail;

		// GitStatusText's output. Unset when there is nothing to say about git at all — no
		// folder open, or a head not delivered yet.
		std::optional<GitStatusSegment> Git;
	};

	// Which piece of the bar was clicked this frame. The bar itself does nothing with a
	// click: what a click opens is the layer's business, and the bar stays read-only.
	enum class StatusBarClick
	{
		None,
		GitBranch,
		GitChanges
	};

	StatusBarClick DrawStatusBar(const Core::DocumentSet& documents,
					   const std::filesystem::path& root,
					   const StatusMessages& messages);

}
