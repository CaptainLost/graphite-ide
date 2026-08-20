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

	// What a drop asks the application to do. Target and Root are one decision rather than
	// two optional paths, because opening a folder and opening a solution both replace the
	// workspace: a drop carrying each of them has to pick one.
	struct DropRouting
	{
		DropTarget Target = DropTarget::None;
		std::filesystem::path Root;
		std::vector<std::filesystem::path> Files;
	};

	using DirectoryPredicate = std::function<bool(const std::filesystem::path&)>;

	// Sorts what was dropped on the window into the workspace to open and the documents to
	// open in it. The first folder or solution encountered claims the workspace and any
	// later one is ignored; every other file becomes a document, in the order dropped.
	//
	// Whether a path is a directory is asked rather than looked up, so the rule can be read
	// without a file system to read it against.
	DropRouting ClassifyDrop(const std::vector<std::filesystem::path>& paths,
							 const DirectoryPredicate& isDirectory);

	// Whether acting on the drop would move the workspace out from under whatever is open, so
	// the application knows when to ask first. An empty currentRoot means nothing is open, and
	// nothing is what a drop can take away.
	//
	// A solution counts too: opening one from outside the current root takes the root with it.
	bool ReplacesWorkspace(const DropRouting& routing,
						   const std::filesystem::path& currentRoot);
}
