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
			std::string label;
			std::string pattern;
		};

		static Result<std::filesystem::path> PickFolder(const std::string& title);

		static Result<std::filesystem::path> PickFile(
			const std::string& title, const std::vector<Filter>& filters);
	};
}