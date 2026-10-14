#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace Graphite {

	struct CommandLine
	{
		// WARNING: Redirects only the user's files. What a folder keeps under <folder>/.graphite
		// is not redirected, so two instances with one folder open still share it.
		std::optional<std::filesystem::path> UserData;

		std::filesystem::path Folder;
	};

	CommandLine ParseCommandLine(const std::vector<std::wstring>& arguments);

	std::vector<std::wstring> ReadProcessCommandLine();
}
