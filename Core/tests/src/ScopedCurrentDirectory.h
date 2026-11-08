#pragma once

#include <filesystem>
#include <system_error>

// Makes a directory this process's current one, and puts the previous one back on every path out of a test, a failed
// REQUIRE included. Declared after a TempDirectory it points into, it gives the directory back before that is removed.
class ScopedCurrentDirectory
{
public:
	explicit ScopedCurrentDirectory(const std::filesystem::path& directory) : m_Previous(std::filesystem::current_path())
	{
		std::error_code changeError;
		std::filesystem::current_path(directory, changeError);

		m_Changed = !changeError;
	}

	~ScopedCurrentDirectory()
	{
		std::error_code ignoredRestoreError;
		std::filesystem::current_path(m_Previous, ignoredRestoreError);
	}

	ScopedCurrentDirectory(const ScopedCurrentDirectory&) = delete;
	ScopedCurrentDirectory& operator=(const ScopedCurrentDirectory&) = delete;

	bool WasChanged() const { return m_Changed; }

private:
	std::filesystem::path m_Previous;
	bool m_Changed = false;
};
