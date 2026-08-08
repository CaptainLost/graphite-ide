#pragma once

#include <filesystem>
#include <string>
#include <string_view>

namespace graphite
{
    /// Settings that belong to a folder rather than to the user, kept in
    /// `<workspace>/.graphite/workspace.json`.
    ///
    /// The split is by what the setting is about. `graphite::Settings` beside the executable
    /// holds what is true of the user — the last folder opened, how ignored files are
    /// drawn. This holds what is true of one project: which solution is selected, which
    /// mode the explorer is in. A global file could only hold one of those at a time,
    /// with every workspace overwriting the last.
    ///
    /// Constructed per workspace and thrown away when it closes. Reading a missing file
    /// gives empty values, which is the correct answer on first open.
    class WorkspaceSettings
    {
    public:
        /// `root` is the workspace folder, not the `.graphite` directory inside it.
        explicit WorkspaceSettings(std::filesystem::path root);

        [[nodiscard]] std::string Get(std::string_view key) const;

        /// Creates `.graphite` if it is missing. A failure is logged and swallowed:
        /// losing a view preference must never stop the IDE doing its job.
        void Set(std::string_view key, std::string value);

    private:
        [[nodiscard]] std::filesystem::path FilePath() const;

        std::filesystem::path m_root;
    };
}
