#pragma once

#include "Core/Base/Result.h"
#include "Core/Git/PatchMatch.h"

#include <filesystem>

namespace Graphite {

	// Format, little-endian: magic "GPI1", a 4-byte count, then per commit its 20-byte id, a 1-byte flag (1: the
	// commit has a patch, 0: it has none) and a 20-byte patch id (zeros when the flag is 0).

	// Adds what the file holds to the cache. A missing, foreign or damaged file adds nothing.
	void LoadPatchIds(const std::filesystem::path& file, Core::PatchIdCache& cache);

	Core::Result<void> SavePatchIds(const std::filesystem::path& file, const Core::PatchIdCache& cache);
}
