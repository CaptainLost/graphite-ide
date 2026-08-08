#pragma once

#include "Core/Base/Result.h"

#include <filesystem>
#include <memory>
#include <string>

namespace core::sln
{
    /// A GUID in the braced uppercase form both solution formats use.
    ///
    /// Randomly generated rather than asked of the operating system, which is all a
    /// version 4 UUID is — so there is no platform branch here and no empty result for a
    /// caller to check.
    [[nodiscard]] std::string NewGuid();

    /// Edits a solution file in place. Everything not touched — formatting, comments, and
    /// the sections this build does not model — survives byte for byte.
    ///
    /// Operations accumulate in memory; nothing reaches disk until Save. An editor
    /// destroyed without saving changes nothing.
    ///
    /// Each operation is all-or-nothing against the in-memory text: one that fails part
    /// way leaves the text as it found it, so a later Save cannot commit half a change.
    class SolutionEditor
    {
    public:
        virtual ~SolutionEditor() = default;

        SolutionEditor(const SolutionEditor&) = delete;
        SolutionEditor& operator=(const SolutionEditor&) = delete;

        /// Reads the file and prepares it for editing. Fails with Unsupported for a
        /// format this build cannot write.
        [[nodiscard]] static Result<std::unique_ptr<SolutionEditor>> Open(
            const std::filesystem::path& solution);

        /// `folder` is a logical path such as "App/Sub", empty for the solution root.
        /// Intermediate folders are created as needed.
        [[nodiscard]] virtual Result<void> AddProject(const std::filesystem::path& project,
                                                      const std::string& folder) = 0;

        /// Takes the project out of the solution. Never touches the project's own files.
        [[nodiscard]] virtual Result<void> RemoveProject(
            const std::filesystem::path& project) = 0;

        [[nodiscard]] virtual Result<void> AddFolder(const std::string& folder) = 0;

        /// Refuses a folder that still holds anything, rather than deleting a subtree by
        /// implication.
        [[nodiscard]] virtual Result<void> RemoveFolder(const std::string& folder) = 0;

        [[nodiscard]] virtual Result<void> RenameFolder(const std::string& folder,
                                                        const std::string& name) = 0;

        [[nodiscard]] virtual Result<void> MoveProject(const std::filesystem::path& project,
                                                       const std::string& folder) = 0;

        /// Writes to a temporary, reads it back with this build's own parser to check the
        /// result is still a solution this build can read, and only then replaces the file.
        /// A malformed write therefore never reaches the file the user's Visual Studio has
        /// open. The read-back does not check that the change is present — only that what
        /// was written parses.
        [[nodiscard]] virtual Result<void> Save() = 0;

    protected:
        SolutionEditor() = default;
    };
}
