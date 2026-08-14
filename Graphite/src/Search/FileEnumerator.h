#pragma once

#include "Core/Jobs/CancellationToken.h"

#include <filesystem>
#include <functional>

namespace Graphite {

	struct Solution;

	// True means "leave this out". A directory answered true is not descended into at all,
	// which is the point: rejecting bin/ at its own entry is what makes this fast, not filtering
	// its contents afterwards.
	using IgnorePredicate = std::function<bool(const std::filesystem::path& path,
											   bool isDirectory)>;

	struct EnumerationInput
	{
		std::filesystem::path Root;

		// Empty means nothing is ignored.
		IgnorePredicate Ignored;
	};

	// Calls onFile for every file under Root, in no promised order. Checks the token between
	// entries and returns early when it is set.
	void EnumerateFiles(const EnumerationInput& input, const Core::CancellationToken& token,
						const std::function<void(const std::filesystem::path&)>& onFile);

	// The files the solution's projects declare. Complete without any walking: ReadSolution
	// fills every project's content when it loads the solution.
	void EnumerateProjectFiles(const Solution& solution,
							   const std::function<void(const std::filesystem::path&)>& onFile);
}
