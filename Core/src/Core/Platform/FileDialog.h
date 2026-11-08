#pragma once

#include "Core/Base/Result.h"

#include <filesystem>
#include <string>
#include <vector>

namespace Core {

	class FileDialog
	{
	public:
		struct Filter
		{
			std::string Label;
			std::string Pattern;
		};

		static Result<std::filesystem::path> PickFolder(const std::string& title);

		static Result<std::filesystem::path> PickFile(
			const std::string& title, const std::vector<Filter>& filters);

		// The system's save dialog, opened in the suggested file's folder with its name filled in; it asks before a file
		// is written over. An empty path when the user cancels, as the pickers give.
		static Result<std::filesystem::path> PickSaveFile(const std::string& title, const std::filesystem::path& suggested);
	};
}