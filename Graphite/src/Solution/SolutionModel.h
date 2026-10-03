#pragma once

#include <cstddef>
#include <filesystem>
#include <string>
#include <vector>

namespace Graphite {

	struct ContentFile
	{
		std::string Name;
		std::filesystem::path Path;

		// A project may list a file that is gone from the disk. A project that lists its
		// files from the disk itself never has a missing one.
		bool Exists = true;
	};

	struct ContentFolder
	{
		std::string Name;
		std::vector<ContentFolder> Folders;
		std::vector<ContentFile> Files;
	};

	struct ProjectContent
	{
		ContentFolder Root;

		std::vector<std::string> References;
	};

	struct Project
	{
		std::string DisplayName;
		std::filesystem::path Path;
		std::string TypeId;

		std::vector<std::string> Dependencies;

		ProjectContent Content;

		bool ContentRead = false;

		bool Exists = true;
	};

	struct Folder
	{
		std::string Name;
		std::vector<Folder> Folders;
		std::vector<Project> Projects;
	};

	struct Solution
	{
		std::filesystem::path Path;
		std::string Name;
		Folder Root;
		std::size_t ProjectCount = 0;
	};
}