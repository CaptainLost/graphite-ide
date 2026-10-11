#pragma once

#include "Core/Base/Result.h"

#include <filesystem>
#include <string_view>

namespace Core {

	class Shell
	{
	public:
		static Result<void> RevealInFileBrowser(const std::filesystem::path& path);

		// Only an https address is opened: anything else could name a program to run.
		static Result<void> OpenInBrowser(std::string_view url);
	};
}