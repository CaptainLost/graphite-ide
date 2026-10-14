#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <vector>

namespace Graphite {

	enum class DropTarget : std::uint8_t
	{
		None,
		Folder,
		Solution
	};

	struct DropRouting
	{
		DropTarget Target = DropTarget::None;
		std::filesystem::path Root;
		std::vector<std::filesystem::path> Files;
	};

	using DirectoryPredicate = std::function<bool(const std::filesystem::path&)>;

	DropRouting ClassifyDrop(const std::vector<std::filesystem::path>& paths,
							 const DirectoryPredicate& isDirectory);

	bool ReplacesWorkspace(const DropRouting& routing,
						   const std::filesystem::path& currentRoot);
}
