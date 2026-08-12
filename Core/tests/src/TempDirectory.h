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

        m_Path = std::filesystem::temp_directory_path() /
                 ("core-tests-" + std::to_string(id) + "-" + std::to_string(::_getpid()));

        std::error_code ec;
        std::filesystem::remove_all(m_Path, ec);
        std::filesystem::create_directories(m_Path, ec);

        // Resolved once the directory exists, so tests can compare this against a path
        // the OS reported back to them. TEMP may be spelled with a short 8.3 name or in
        // a different case than the filesystem stores, and either would make an
        // otherwise correct comparison fail on someone else's machine.
        if (auto resolved = std::filesystem::canonical(m_Path, ec); !ec)
            m_Path = std::move(resolved);
    }

    ~TempDirectory()
    {
        std::error_code ec;
        std::filesystem::remove_all(m_Path, ec);
    }

    TempDirectory(const TempDirectory&) = delete;
    TempDirectory& operator=(const TempDirectory&) = delete;

    [[nodiscard]] const std::filesystem::path& Path() const noexcept { return m_Path; }

    std::filesystem::path MakeDir(const std::string& relative)
    {
        const std::filesystem::path full = m_Path / relative;
        std::error_code ec;
        std::filesystem::create_directories(full, ec);
        return full;
    }

    std::filesystem::path MakeFile(const std::string& relative, const std::string& contents = "")
    {
        const std::filesystem::path full = m_Path / relative;
        std::error_code ec;
        std::filesystem::create_directories(full.parent_path(), ec);

        std::ofstream stream{full, std::ios::binary | std::ios::trunc};
        stream << contents;
        return full;
    }

private:
    std::filesystem::path m_Path;
};
