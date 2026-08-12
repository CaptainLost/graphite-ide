#pragma once

#include <cstddef>
#include <filesystem>
#include <string>
#include <vector>

namespace Graphite {

	struct ContentFile
	{
		std::string name;
		std::filesystem::path path;
	};

	struct ContentFolder
	{
		std::string name;
		std::vector<ContentFolder> folders;
		std::vector<ContentFile> files;
	};

	struct ProjectContent
	{
		ContentFolder root;

		std::vector<std::string> references;
	};

	struct Project
	{
		std::string displayName;
		std::filesystem::path path;
		std::string typeId;

		std::vector<std::string> dependencies;

		ProjectContent content;

		bool contentRead = false;

		bool exists = true;
	};

	struct Folder
	{
		std::string name;
		std::vector<Folder> folders;
		std::vector<Project> projects;
	};

	struct Solution
	{
		std::filesystem::path path;
		std::string name;
		Folder root;
		std::size_t projectCount = 0;
	};
}