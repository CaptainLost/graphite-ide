#pragma once

#include "Core/Sln/SolutionEditor.h"

#include <cstddef>
#include <filesystem>
#include <string>

namespace core::sln
{
    /// Edits a .slnx by splicing its text. pugixml locates the elements; nothing is ever
    /// re-serialised, because saving a parsed document rewrites every self-closing tag —
    /// a diff touching the whole file for one added line.
    class SlnxEditor final : public SolutionEditor
    {
    public:
        SlnxEditor(std::filesystem::path path, std::string text);

        [[nodiscard]] Result<void> AddProject(const std::filesystem::path& project,
                                              const std::string& folder) override;
        [[nodiscard]] Result<void> RemoveProject(const std::filesystem::path& project) override;
        [[nodiscard]] Result<void> AddFolder(const std::string& folder) override;
        [[nodiscard]] Result<void> RemoveFolder(const std::string& folder) override;
        [[nodiscard]] Result<void> RenameFolder(const std::string& folder,
                                                const std::string& name) override;
        [[nodiscard]] Result<void> MoveProject(const std::filesystem::path& project,
                                               const std::string& folder) override;
        [[nodiscard]] Result<void> Save() override;

    private:
        /// Offset of the `<Solution>` or `<Folder>` element the logical path names, and
        /// whether it had to be created. Empty `folder` means the solution element.
        [[nodiscard]] Result<std::size_t> FindContainer(const std::string& folder) const;

        /// Offset of the `<Project>` element naming this file, or an error when the
        /// solution does not have it.
        [[nodiscard]] Result<std::size_t> FindProject(const std::filesystem::path& project) const;

        /// Inserts `element` as the first child of the container at `offset`, expanding a
        /// self-closing container into an open/close pair when it has no children yet.
        void InsertChild(std::size_t offset, const std::string& element);

        std::filesystem::path m_path;
        std::string m_text;
    };
}
