#pragma once

#include "Language/Analysis/AnalysisStore.h"

#include <doctest/doctest.h>

#include <chrono>
#include <filesystem>
#include <thread>

inline constexpr int kStorePollAttempts = 200;
inline constexpr std::chrono::milliseconds kStorePollInterval{10};

inline void WaitForStoreWrites(Graphite::AnalysisStore& store)
{
	for (int attempt = 0; attempt < kStorePollAttempts && !store.IsQuiet(); ++attempt)
	{
		std::this_thread::sleep_for(kStorePollInterval);
	}

	REQUIRE(store.IsQuiet());
}

inline void WaitForStoreReady(Graphite::AnalysisStore& store)
{
	for (int attempt = 0; attempt < kStorePollAttempts && !store.IsReady(); ++attempt)
	{
		store.Poll();
		std::this_thread::sleep_for(kStorePollInterval);
	}

	REQUIRE(store.IsReady());
}

inline std::filesystem::file_time_type FileTimeAt(int seconds)
{
	return std::filesystem::file_time_type{} + std::chrono::seconds(seconds);
}
