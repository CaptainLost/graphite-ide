#pragma once

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
	};

	void DrawStatusBar(const Core::DocumentSet& documents,
					   const std::filesystem::path& root,
					   const StatusMessages& messages);

}
