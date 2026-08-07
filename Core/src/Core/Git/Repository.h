#pragma once

#include "Core/Base/Result.h"

#include <filesystem>

struct git_repository;

namespace core::git
{
    /// A libgit2 repository handle with a C++ lifetime. Move-only: two owners would free
    /// the same handle twice.
    class Repository
    {
    public:
        /// Finds the repository containing the given path and opens it. Reports NotFound
        /// when the path is not inside a working tree — which is the ordinary case for a
        /// folder the user opened that simply is not under version control.
        static Result<Repository> Open(const std::filesystem::path& anyPathInside);

        ~Repository();

        Repository(Repository&& other) noexcept;
        Repository& operator=(Repository&& other) noexcept;

        Repository(const Repository&) = delete;
        Repository& operator=(const Repository&) = delete;

        /// Answers git's own ignore question, including nested rules files, negations and
        /// the global and repository-local excludes. A path outside the working tree, or
        /// any failure, reads as not ignored — the tree must stay usable when the answer
        /// is unavailable.
        ///
        /// Not cheap: each call stats the path, then walks a stack of .gitignore files
        /// from the working directory down to it, re-stating each one along the way.
        /// It is meant for filling a cached flag when a directory is loaded, not for
        /// calling from a render loop.
        [[nodiscard]] bool IsIgnored(const std::filesystem::path& path) const;

        [[nodiscard]] const std::filesystem::path& WorkingDirectory() const noexcept
        {
            return m_workingDirectory;
        }

    private:
        Repository() = default;

        git_repository* m_handle = nullptr;
        std::filesystem::path m_workingDirectory;
    };
}
