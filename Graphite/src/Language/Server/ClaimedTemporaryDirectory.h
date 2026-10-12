#pragma once

#include "Core/Base/Result.h"
#include "Core/FileSystem/FileLock.h"

#include <filesystem>

namespace Graphite {

	// The claim holds a lock file inside the directory open, and a directory with a file open cannot be renamed,
	// which is what keeps a sweep from any Graphite away from it. A Graphite that dies lets go of it too.
	class ClaimedTemporaryDirectory
	{
	public:
		static Core::Result<ClaimedTemporaryDirectory> Claim(const std::filesystem::path& parent);

		const std::filesystem::path& GetPath() const;

	private:
		ClaimedTemporaryDirectory(std::filesystem::path path, Core::FileLock lock);

		std::filesystem::path m_Path;
		Core::FileLock m_Lock;
	};

	void SweepUnclaimedTemporaryDirectories(const std::filesystem::path& parent);
}
