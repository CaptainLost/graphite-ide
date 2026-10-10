#pragma once

#include "Core/Base/Result.h"
#include "Core/Jobs/CancellationToken.h"

#include <filesystem>

namespace Core {

	// An archive with one unsafe entry name extracts nothing. A cancelled or failed extraction leaves what it
	// had already written; removing it is the caller's job.
	Result<void> ExtractZip(const std::filesystem::path& archive, const std::filesystem::path& destination,
							const CancellationToken& token);
}
