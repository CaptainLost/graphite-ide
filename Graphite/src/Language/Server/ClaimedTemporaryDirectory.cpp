#include "Language/Server/ClaimedTemporaryDirectory.h"

#include "Language/Install/InstalledServer.h"
#include "Language/Install/PackageNames.h"

#include "Core/FileSystem/FileSystem.h"

#include <format>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace Graphite {

	namespace {

		constexpr int kHighestDirectoryNumber = 1000;

		constexpr std::string_view kClaimLockFileName = "graphite.lock";

		void RemoveQuietly(const std::filesystem::path& path)
		{
			std::error_code ignoredError;
			std::filesystem::remove_all(path, ignoredError);
		}

		std::filesystem::path NumberedDirectory(const std::filesystem::path& parent, int number)
		{
			return parent / std::to_string(number);
		}
	}

	ClaimedTemporaryDirectory::ClaimedTemporaryDirectory(std::filesystem::path path, Core::FileLock lock)
		: m_Path(std::move(path)), m_Lock(std::move(lock))
	{
	}

	Core::Result<ClaimedTemporaryDirectory> ClaimedTemporaryDirectory::Claim(const std::filesystem::path& parent)
	{
		std::error_code parentError;
		std::filesystem::create_directories(parent, parentError);

		if (parentError)
		{
			return Core::Error{.Code = Core::ErrorCode::IoFailure,
							   .Message = std::format("cannot create '{}': {}", Core::ToUtf8(parent), parentError.message())};
		}

		for (int number = 1; number <= kHighestDirectoryNumber; ++number)
		{
			const std::filesystem::path candidate = NumberedDirectory(parent, number);
			std::error_code createError;

			// WARNING: False for a directory that already exists, which is another Graphite's or one a sweep has yet to remove.
			const bool createdHere = std::filesystem::create_directory(candidate, createError);

			if (!createdHere)
			{
				continue;
			}

			Core::Result<Core::FileLock> lock = Core::FileLock::TryAcquire(candidate / Core::FromUtf8(kClaimLockFileName));

			if (!lock)
			{
				RemoveQuietly(candidate);
				return lock.GetError();
			}

			return ClaimedTemporaryDirectory{candidate, std::move(lock.GetValue())};
		}

		return Core::Error{.Code = Core::ErrorCode::IoFailure,
						   .Message = std::format("no directory numbered up to {} could be created in '{}'", kHighestDirectoryNumber,
												  Core::ToUtf8(parent))};
	}

	const std::filesystem::path& ClaimedTemporaryDirectory::GetPath() const
	{
		return m_Path;
	}

	void SweepUnclaimedTemporaryDirectories(const std::filesystem::path& parent)
	{
		const Core::Result<std::vector<Core::DirectoryEntry>> entries = Core::ReadDirectory(parent);

		if (!entries)
		{
			return;
		}

		for (const Core::DirectoryEntry& entry : entries.GetValue())
		{
			const bool setAsideByAnEarlierSweep = IsTemporaryName(entry.Name);

			if (entry.IsDirectory && !setAsideByAnEarlierSweep)
			{
				RemoveDirectoryWhole(entry.Path);
				continue;
			}

			RemoveQuietly(entry.Path);
		}
	}
}
