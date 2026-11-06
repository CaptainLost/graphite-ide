#pragma once

#include "Core/Base/Result.h"
#include "Core/Git/Content/ContentCheck.h"
#include "Core/Git/Oid.h"

#include <compare>
#include <filesystem>
#include <map>
#include <string>

namespace Graphite {

	// A verdict holds for one pair of tips and never changes: commits are immutable.
	struct ContentKey
	{
		Core::Oid BranchTip;
		Core::Oid LineTip;

		friend auto operator<=>(const ContentKey&, const ContentKey&) = default;
	};

	// The first Merged verdict found for a branch tip and the full ref name of the line it was found against,
	// kept so that the landing is not lost when the line later moves on.
	struct ContentFound
	{
		std::string Line;
		Core::ContentVerdict Verdict;
	};

	// Unknown verdicts are never stored: they say a check could not be made, not what the answer is.
	struct ContentCache
	{
		std::map<ContentKey, Core::ContentVerdict> Verdicts;
		std::map<Core::Oid, ContentFound> Found;
	};

	// A missing or damaged file, or one of another "version", gives an empty cache.
	ContentCache LoadContentCache(const std::filesystem::path& file);

	Core::Result<void> SaveContentCache(const std::filesystem::path& file, const ContentCache& cache);
}
