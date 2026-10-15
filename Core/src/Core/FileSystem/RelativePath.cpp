#include "corepch.h"

#include "Core/FileSystem/RelativePath.h"

#include "Core/FileSystem/FileName.h"
#include "Core/FileSystem/FileSystem.h"

namespace Core {

	bool IsSafeRelativePath(std::string_view path)
	{
		if (path.empty() || IsPathSeparator(path.front()))
		{
			return false;
		}

		std::size_t segmentStart = 0;

		while (segmentStart <= path.size())
		{
			const std::size_t separator = path.find_first_of("/\\", segmentStart);
			const std::size_t segmentEnd = separator == std::string_view::npos ? path.size() : separator;
			const std::string_view segment = path.substr(segmentStart, segmentEnd - segmentStart);

			if (!segment.empty() && !IsValidFileName(segment))
			{
				return false;
			}

			segmentStart = segmentEnd + 1;
		}

		return true;
	}
}
