#include "WorkspaceSettings.h"

#include "Core/Base/JsonFile.h"
#include "Core/Base/Log.h"
#include "Core/Fs/FileOperations.h"

#include <filesystem>
#include <utility>

namespace ide
{
    namespace
    {
        /// Named after the application. Ignored by git the way .vs is: it holds view
        /// state, not configuration anyone would want to share.
        constexpr const char* kDirectory = ".graphite";
        constexpr const char* kFile = "workspace.json";
    }

    WorkspaceSettings::WorkspaceSettings(std::filesystem::path root) : m_root(std::move(root))
    {
    }

    std::filesystem::path WorkspaceSettings::FilePath() const
    {
        return m_root / kDirectory / kFile;
    }

    std::string WorkspaceSettings::Get(std::string_view key) const
    {
        if (m_root.empty())
            return {};

        return core::JsonFile::Load(FilePath()).Get(key);
    }

    void WorkspaceSettings::Set(std::string_view key, std::string value)
    {
        if (m_root.empty())
            return;

        // Created on the first write rather than when the folder opens, so browsing a
        // folder read-only never leaves anything behind in it.
        const std::filesystem::path directory = m_root / kDirectory;

        // MakeDirectory reports an error when the directory already exists, so the
        // ordinary case — every write after the first — has to be guarded here rather
        // than treated as a failure.
        std::error_code code;
        if (!std::filesystem::exists(directory, code))
        {
            if (const auto made = core::fs::MakeDirectory(directory); !made)
            {
                CORE_WARN("Cannot create '{}': {}", directory.string(), made.GetError().message);
                return;
            }
        }

        core::JsonFile file = core::JsonFile::Load(FilePath());
        file.Set(key, std::move(value));

        if (!file.Save(FilePath()))
            CORE_WARN("Cannot write '{}'", FilePath().string());
    }
}
