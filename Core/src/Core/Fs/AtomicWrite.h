#pragma once

#include "Core/Base/Result.h"

#include <filesystem>
#include <functional>
#include <string_view>

namespace Core {

	Result<std::filesystem::path> PickAvailablePath(
		const std::function<std::filesystem::path(int)>& candidateFor);

	Result<void> WriteFileAtomically(const std::filesystem::path& path,
									 std::string_view bytes);
}