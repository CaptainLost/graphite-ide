#pragma once

#include "Core/Git/Oid.h"

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
		// Both stay zero when there is no upstream; a caller must check Upstream before
		// reading them, because zero and zero also spells "in sync".
		std::size_t Ahead = 0;
		std::size_t Behind = 0;
	};
}
