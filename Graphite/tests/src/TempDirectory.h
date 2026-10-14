#pragma once

#include <atomic>
#include <filesystem>
#include <fstream>
#include <process.h>
#include <string>
#include <system_error>
#include <utility>

class TempDirectory
{
public:
	TempDirectory()
	{
		static std::atomic<int> nextDirectoryNumber{0};
		const int directoryNumber = nextDirectoryNumber.fetch_add(1);

		m_Path = std::filesystem::temp_directory_path() /
				 ("graphite-tests-" + std::to_string(directoryNumber) + "-" + std::to_string(::_getpid()));

		std::error_code errorCode;
		std::filesystem::remove_all(m_Path, errorCode);
		std::filesystem::create_directories(m_Path, errorCode);

		// WARNING: The path is resolved once the directory exists. TEMP may be spelled with a short
		// 8.3 name or in another case than the filesystem stores, which would fail comparisons
		// against paths the OS reports back.
		if (std::filesystem::path resolved = std::filesystem::canonical(m_Path, errorCode); !errorCode)
		{
			m_Path = std::move(resolved);
		}
	}

	~TempDirectory()
	{
		std::error_code errorCode;
		std::filesystem::remove_all(m_Path, errorCode);
	}

	TempDirectory(const TempDirectory&) = delete;
	TempDirectory& operator=(const TempDirectory&) = delete;

	const std::filesystem::path& GetPath() const { return m_Path; }

	std::filesystem::path MakeDirectory(const std::string& relative)
	{
		const std::filesystem::path full = m_Path / relative;
		std::error_code errorCode;
		std::filesystem::create_directories(full, errorCode);
		return full;
	}

	std::filesystem::path MakeFile(const std::string& relative, const std::string& contents = "")
	{
		const std::filesystem::path full = m_Path / relative;
		std::error_code errorCode;
		std::filesystem::create_directories(full.parent_path(), errorCode);

		std::ofstream stream{full, std::ios::binary | std::ios::trunc};
		stream << contents;
		return full;
	}

private:
	std::filesystem::path m_Path;
};
