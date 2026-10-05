#pragma once

#include "Solution/SolutionModel.h"

#include <string_view>

template<typename FolderType>
const FolderType* FindChildFolder(const FolderType& parent, std::string_view name)
{
	for (const FolderType& folder : parent.Folders)
	{
		if (folder.Name == name)
		{
			return &folder;
		}
	}

	return nullptr;
}

inline bool HasFileNamed(const Graphite::ContentFolder& folder, std::string_view name)
{
	for (const Graphite::ContentFile& file : folder.Files)
	{
		if (file.Name == name)
		{
			return true;
		}
	}

	return false;
}
