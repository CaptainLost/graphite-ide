#pragma once

#include "Core/Jobs/CancellationToken.h"

#include <cstddef>
#include <filesystem>
#include <string_view>
#include <vector>

namespace Graphite {

	inline constexpr std::size_t kMostProjectsLoaded = 500;

	struct ProjectsBelow
	{
		std::vector<std::filesystem::path> Kept;
		std::size_t DroppedAboveLimit = 0;
	};

	// WARNING: Walks the whole tree below the root; call it on a worker, never in a frame.
	ProjectsBelow FindProjectsBelow(const std::filesystem::path& root, std::string_view extension, std::size_t most,
									const Core::CancellationToken& token);
}
