#include "corepch.h"

#include "Core/Git/Repository.h"

#include "Core/Base/Log.h"
#include "Core/Fs/FileSystem.h"

#include <git2.h>

#include <utility>

namespace core::git
{
    namespace
    {
        /// libgit2 requires initialisation before any other call and keeps a reference
        /// count, so initialising once per process on first use is both sufficient and
        /// safe. A function-local static gives us that with C++'s own thread-safe
        /// initialisation rather than a hand-rolled flag.
        void EnsureInitialised()
        {
            static const int result = git_libgit2_init();

            if (result < 0)
                CORE_ERROR("libgit2 failed to initialise (code {})", result);
        }
    }

    Result<Repository> Repository::Open(const std::filesystem::path& anyPathInside)
    {
        EnsureInitialised();

        git_repository* handle = nullptr;
        const std::string path = fs::ToUtf8(anyPathInside);

        // ceiling_dirs is null so the search walks up to the filesystem root, matching
        // what the git command line does from a subdirectory.
        const int result = git_repository_open_ext(&handle, path.c_str(), 0, nullptr);

        if (result != 0 || handle == nullptr)
        {
            // libgit2's own message names the real cause — a .git file pointing at a
            // deleted worktree reads very differently from an unreadable directory, and
            // collapsing both into "not a repository" throws that away.
            const git_error* detail = git_error_last();
            const std::string message =
                detail != nullptr && detail->message != nullptr ? detail->message : "unknown error";

            ErrorCode code = ErrorCode::IoFailure;

            if (result == GIT_ENOTFOUND)
                code = ErrorCode::NotFound;
            else if (result == GIT_EAUTH)
                code = ErrorCode::AccessDenied;

            return Error{code, std::format("cannot open a git repository at '{}': {}", path, message)};
        }

        Repository repository;
        repository.m_handle = handle;

        if (const char* workdir = git_repository_workdir(handle); workdir != nullptr)
            repository.m_workingDirectory = fs::FromUtf8(workdir);

        return repository;
    }

    Repository::~Repository()
    {
        if (m_handle != nullptr)
            git_repository_free(m_handle);
    }

    Repository::Repository(Repository&& other) noexcept
        : m_handle(std::exchange(other.m_handle, nullptr)),
          m_workingDirectory(std::move(other.m_workingDirectory))
    {
    }

    Repository& Repository::operator=(Repository&& other) noexcept
    {
        if (this == &other)
            return *this;

        if (m_handle != nullptr)
            git_repository_free(m_handle);

        m_handle = std::exchange(other.m_handle, nullptr);
        m_workingDirectory = std::move(other.m_workingDirectory);
        return *this;
    }

    bool Repository::IsIgnored(const std::filesystem::path& path) const
    {
        if (m_handle == nullptr)
            return false;

        std::error_code code;
        const std::filesystem::path relative =
            std::filesystem::relative(path, m_workingDirectory, code);

        // A leading ".." component means the path sits outside the working tree, and
        // there is nothing for git to have an opinion about. Comparing components rather
        // than the string keeps an in-tree file called "..config" from being mistaken
        // for one.
        const bool outsideTree =
            relative.begin() != relative.end() && *relative.begin() == "..";

        if (code || relative.empty() || outsideTree)
            return false;

        // git speaks forward slashes on every platform; the relative path may carry
        // native separators, so they are translated explicitly below.
        std::string text = fs::ToUtf8(relative);

        for (char& character : text)
        {
            if (character == '\\')
                character = '/';
        }

        int ignored = 0;
        if (git_ignore_path_is_ignored(&ignored, m_handle, text.c_str()) != 0)
        {
            CORE_TRACE("Cannot determine ignore status for '{}'", text);
            return false;
        }

        return ignored != 0;
    }
}
