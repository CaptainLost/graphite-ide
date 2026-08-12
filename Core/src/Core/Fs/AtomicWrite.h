#pragma once

#include "Core/Base/Result.h"

#include <filesystem>
#include <functional>
#include <string_view>

namespace Core {

	Result<std::filesystem::path> PickAvailablePath(
		const std::function<std::filesystem::path(int)>& candidateFor);

	// The temp file goes beside the target, not in the temp directory: same volume, and it
	// inherits the target directory's ACLs. ReplaceFileW rather than rename because it
	// preserves ACLs, creation time and file identity. This is the one platform split in
	// Core whose #else must not return Unsupported. The stream is closed but not flushed,
	// so this protects against process death, not power loss.
	Result<void> WriteFileAtomically(const std::filesystem::path& path,
									 std::string_view bytes);
}