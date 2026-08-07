#pragma once

#include <atomic>
#include <filesystem>
#include <fstream>
#include <process.h>
#include <string>
#include <system_error>

/// Creates a unique directory under the system temp path and removes it on destruction.
/// Used by tests that need real files on disk.
class TempDirectory
{
public:
    TempDirectory()
    {
        static std::atomic<int> counter{0};
        const int id = counter.fetch_add(1);

        m_path = std::filesystem::temp_directory_path() /
                 ("core-tests-" + std::to_string(id) + "-" + std::to_string(::_getpid()));

        std::error_code ec;
        std::filesystem::remove_all(m_path, ec);
        std::filesystem::create_directories(m_path, ec);
    }

    ~TempDirectory()
    {
        std::error_code ec;
        std::filesystem::remove_all(m_path, ec);
    }

    TempDirectory(const TempDirectory&) = delete;
    TempDirectory& operator=(const TempDirectory&) = delete;

    [[nodiscard]] const std::filesystem::path& Path() const noexcept { return m_path; }

    std::filesystem::path MakeDir(const std::string& relative)
    {
        const std::filesystem::path full = m_path / relative;
        std::error_code ec;
        std::filesystem::create_directories(full, ec);
        return full;
    }

    std::filesystem::path MakeFile(const std::string& relative, const std::string& contents = "")
    {
        const std::filesystem::path full = m_path / relative;
        std::error_code ec;
        std::filesystem::create_directories(full.parent_path(), ec);

        std::ofstream stream{full, std::ios::binary | std::ios::trunc};
        stream << contents;
        return full;
    }

private:
    std::filesystem::path m_path;
};
