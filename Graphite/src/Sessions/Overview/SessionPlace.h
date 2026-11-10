#pragma once

#include "Sessions/Pricing.h"
#include "Sessions/SessionSummary.h"

#include <filesystem>
#include <string>

namespace Graphite {

	struct SessionPlace
	{
		std::filesystem::path Repository;
		std::string Worktree;

		friend bool operator==(const SessionPlace&, const SessionPlace&) = default;
	};

	// WARNING: Only `<repository>\.claude\worktrees\<name>` is a worktree, and an `agent-*` name there counts
	// as the repository itself.
	SessionPlace PlaceOfDirectory(const std::filesystem::path& cwd);

	SessionPlace PlaceOfSession(const SessionSummary& summary, const Pricing& pricing);
}
