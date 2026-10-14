#pragma once

#include "Core/Jobs/CancellationToken.h"

#include <filesystem>
#include <functional>

namespace Graphite {

	struct Solution;

	using IgnorePredicate = std::function<bool(const std::filesystem::path& path,
											   bool isDirectory)>;

	struct EnumerationInput
	{
		std::filesystem::path Root;
		IgnorePredicate Ignored;
	};

	void EnumerateFiles(const EnumerationInput& input, const Core::CancellationToken& token,
						const std::function<void(const std::filesystem::path&)>& onFile);

	void EnumerateProjectFiles(const Solution& solution,
							   const std::function<void(const std::filesystem::path&)>& onFile);
}
