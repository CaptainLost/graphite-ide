#pragma once

#include "Core/Git/Oid.h"
#include "Core/Git/Signature.h"

#include <cstddef>
#include <vector>

namespace Core {

	inline constexpr std::size_t kMostOwnCommitsPerTip = 5000;

	struct OwnAuthorsRequest
	{
		// Two branches can share a tip, so a tip may appear more than once.
		std::vector<Oid> Tips;

		// A commit any of these reaches is nobody's own work.
		std::vector<Oid> Hidden;

		std::size_t MaxCommitsPerTip = kMostOwnCommitsPerTip;

		friend bool operator==(const OwnAuthorsRequest&, const OwnAuthorsRequest&) = default;
	};

	struct TipAuthors
	{
		Oid Tip;

		// Newest first, one per e-mail whatever its case, each with the name and time of that
		// author's newest own commit on this tip.
		std::vector<Signature> Authors;

		// Zero for a tip a hidden commit already reaches: a merged branch, or one just created.
		std::size_t OwnCommits = 0;

		// MaxCommitsPerTip stopped the count before the tip's own commits ran out.
		bool Truncated = false;
	};
}
