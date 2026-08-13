#pragma once

#include "Core/Base/Result.h"

#include <filesystem>
#include <functional>
#include <string_view>

namespace Core {

	Result<std::filesystem::path> PickAvailablePath(
		const std::function<std::filesystem::path(int)>& candidateFor);

	// The temporary goes beside the target, not in the temp directory: same volume, and it
	// inherits the target directory's ACLs. The stream is closed but not flushed, so this
	// protects against process death, not power loss.
	Result<void> WriteFileAtomically(const std::filesystem::path& path,
									 std::string_view bytes);
}