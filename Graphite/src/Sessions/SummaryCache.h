#pragma once

#include "Sessions/SessionSummary.h"

#include <filesystem>
#include <vector>

namespace Graphite {

	// Raised whenever SessionSummary gains or changes a field, so a cache written by an older
	// build is dropped instead of read into the wrong shape.
	constexpr int kSummaryCacheVersion = 1;

	// Empty on any problem: the cache only saves time, and a scan rebuilds whatever it lacks.
	std::vector<SessionSummary> LoadSummaryCache(const std::filesystem::path& file);

	bool SaveSummaryCache(const std::filesystem::path& file, const std::vector<SessionSummary>& summaries);
}
