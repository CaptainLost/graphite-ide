#pragma once

#include <miniz.h>

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

struct ZipEntry
{
	std::string Name;
	std::string Contents;
};

// Empty when miniz refuses an entry. Its writer will not take a name starting with '/', which is what
// RenameEntry is for.
inline std::string ZipBytes(const std::vector<ZipEntry>& entries)
{
	mz_zip_archive writer{};

	if (mz_zip_writer_init_heap(&writer, 0, 0) == MZ_FALSE)
	{
		return {};
	}

	for (const ZipEntry& entry : entries)
	{
		const bool added = mz_zip_writer_add_mem(&writer, entry.Name.c_str(), entry.Contents.data(),
												 entry.Contents.size(), static_cast<mz_uint>(MZ_DEFAULT_LEVEL)) != MZ_FALSE;

		if (!added)
		{
			mz_zip_writer_end(&writer);
			return {};
		}
	}

	void* archiveMemory = nullptr;
	std::size_t archiveSize = 0;
	const bool finished = mz_zip_writer_finalize_heap_archive(&writer, &archiveMemory, &archiveSize) != MZ_FALSE;
	std::string archive = finished ? std::string(static_cast<const char*>(archiveMemory), archiveSize) : std::string{};

	mz_free(archiveMemory);
	mz_zip_writer_end(&writer);
	return archive;
}

// The name is stored twice, in the local header and in the central directory, and both copies are rewritten.
// The two names must be the same length.
inline void RenameEntry(std::string& archiveBytes, std::string_view writtenName, std::string_view wantedName)
{
	std::size_t found = archiveBytes.find(writtenName);

	while (found != std::string::npos)
	{
		archiveBytes.replace(found, writtenName.size(), wantedName);
		found = archiveBytes.find(writtenName, found + wantedName.size());
	}
}
