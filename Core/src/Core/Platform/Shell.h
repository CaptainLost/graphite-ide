#pragma once

#include "Core/Base/Result.h"

#include <filesystem>

namespace Core {

	class Shell
	{
	public:
		static Result<void> RevealInFileBrowser(const std::filesystem::path& path);
	};
}