#include "Solution/ProjectContentReader.h"

#include "Core/FileSystem/FileSystem.h"

#include <algorithm>

namespace Graphite {

	namespace {
		bool IsContentFolderNameBefore(const ContentFolder& left, const ContentFolder& right)
		{
			return Core::CompareNames(left.Name, right.Name) < 0;
		}

		bool IsContentFileNameBefore(const ContentFile& left, const ContentFile& right)
		{
			return Core::CompareNames(left.Name, right.Name) < 0;
		}
	}

	void SortContentByName(ContentFolder& folder)
	{
		std::sort(folder.Folders.begin(), folder.Folders.end(), IsContentFolderNameBefore);
		std::sort(folder.Files.begin(), folder.Files.end(), IsContentFileNameBefore);

		for (ContentFolder& child : folder.Folders)
		{
			SortContentByName(child);
		}
	}
}
