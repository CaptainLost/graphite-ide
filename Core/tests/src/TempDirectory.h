#pragma once

#include <atomic>
#include <filesystem>
#include <fstream>
#include <process.h>
#include <string>
#include <system_error>

class TempDirectory
{
public:
	TempDirectory()
	{
		static std::atomic<int> directoriesCreated{0};
		const int directoryNumber = directoriesCreated.fetch_add(1);

		m_Path = std::filesystem::temp_directory_path() /
				 ("core-tests-" + std::to_string(directoryNumber) + "-" + std::to_string(::_getpid()));

		std::error_code ignoredError;
		std::filesystem::remove_all(m_Path, ignoredError);
		std::filesystem::create_directories(m_Path, ignoredError);

		// TEMP may be spelled with a short 8.3 name or in another case than the filesystem stores,
		// so the path is made canonical once it exists, to compare equal to paths the OS reports.
		std::error_code canonicalError;
		if (std::filesystem::path resolved = std::filesystem::canonical(m_Path, canonicalError); !canonicalError)
		{
			m_Path = std::move(resolved);
		}
	}

	~TempDirectory()
	{
		std::error_code ignoredError;
		std::filesystem::remove_all(m_Path, ignoredError);
	}

	TempDirectory(const TempDirectory&) = delete;
	TempDirectory& operator=(const TempDirectory&) = delete;

	const std::filesystem::path& GetPath() const { return m_Path; }

	std::filesystem::path MakeDirectory(const std::string& relativePath)
	{
		const std::filesystem::path fullPath = m_Path / relativePath;
		std::error_code ignoredError;
		std::filesystem::create_directories(fullPath, ignoredError);
		return fullPath;
	}

	std::filesystem::path MakeFile(const std::string& relativePath, const std::string& contents = "")
	{
		const std::filesystem::path fullPath = m_Path / relativePath;
		std::error_code ignoredError;
		std::filesystem::create_directories(fullPath.parent_path(), ignoredError);

		std::ofstream stream{fullPath, std::ios::binary | std::ios::trunc};
		stream << contents;
		return fullPath;
	}

private:
	std::filesystem::path m_Path;
};
