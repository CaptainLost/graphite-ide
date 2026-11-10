#pragma once

#include "Core/Base/Result.h"

#include "Sessions/SessionSummary.h"

#include <filesystem>
#include <vector>

namespace Graphite {

	// WARNING: Raise this whenever SessionSummary gains or changes a field, or a field is worked
	// out differently, so a cache written by an older build is dropped.
	constexpr int kSummaryCacheVersion = 3;

	std::vector<SessionSummary> LoadSummaryCache(const std::filesystem::path& file);

	Core::Result<void> SaveSummaryCache(const std::filesystem::path& file, const std::vector<SessionSummary>& summaries);
}
