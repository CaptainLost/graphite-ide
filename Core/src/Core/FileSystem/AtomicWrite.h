#pragma once

#include "Core/Base/Result.h"

#include <filesystem>
#include <functional>
#include <string_view>

namespace Core {

	Result<std::filesystem::path> PickAvailablePath(
		const std::function<std::filesystem::path(int)>& candidateFor);

	// The stream is closed but not flushed to disk, so a write survives the process dying but
	// not the power failing.
	Result<void> WriteFileAtomically(const std::filesystem::path& target,
									 std::string_view bytes);
}
