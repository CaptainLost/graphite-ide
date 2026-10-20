#pragma once

#include "Core/Base/Result.h"

#include <filesystem>
#include <string_view>

namespace Core {

	class Shell
	{
	public:
		static Result<void> RevealInFileBrowser(const std::filesystem::path& path);

		// Opens the folder itself in the file browser, where RevealInFileBrowser would open its parent
		// with the folder selected.
		static Result<void> OpenFolder(const std::filesystem::path& folder);

		// Only an https address is opened: anything else could name a program to run.
		static Result<void> OpenInBrowser(std::string_view url);
	};
}