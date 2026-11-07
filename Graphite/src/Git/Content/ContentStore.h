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

	// A check's unknown answer (an object it needs is missing, or the line ran past what one check reads) is kept for its
	// pair like any other: the same two tips give the same answer, and the second opening must not ask it again. A check
	// that failed is never stored.
	struct ContentCache
	{
		std::map<ContentKey, Core::ContentVerdict> Verdicts;

		// By branch tip, the first merged verdict found for it, kept so that the landing is not lost when the line later
		// moves on.
		std::map<Core::Oid, Core::ContentVerdict> Found;
	};

	// A missing or damaged file, or one of another "version", gives an empty cache.
	ContentCache LoadContentCache(const std::filesystem::path& file);

	Core::Result<void> SaveContentCache(const std::filesystem::path& file, const ContentCache& cache);
}
