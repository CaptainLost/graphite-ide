#pragma once

#include "Core/Git/Oid.h"

#include <cstddef>
#include <optional>
#include <string>

namespace Core {

	enum class RefKind
	{
		Local,
		Remote
	};

	struct Branch
	{
		// "main" — what a person calls it.
		std::string Name;

		// "refs/heads/main" — what a query needs.
		std::string FullName;

		RefKind Kind = RefKind::Local;

		Oid Tip;

		bool IsHead = false;

		// The remote branch this one tracks, if any.
		std::optional<std::string> Upstream;

		// Commits this branch has that its upstream does not, and the other way around.
		// Both stay zero when the distance is not known; a caller must check
		// DistanceKnown before reading them, because zero and zero also spells "in sync".
		std::size_t Ahead = 0;
		std::size_t Behind = 0;

		// Whether Ahead and Behind were actually computed. False both when there is no
		// upstream and when the distance to it could not be measured — a symbolic
		// upstream, or a graph walk that failed. Zero and zero is a real answer meaning
		// "in sync", so a caller that skips this check cannot tell the two apart.
		bool DistanceKnown = false;
	};
}
