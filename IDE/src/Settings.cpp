#include "Settings.h"

#include "Core/Base/Log.h"

#include <fstream>
#include <string>

namespace ide
{
    namespace
    {
        constexpr const char* kSettingsFile = "ide.ini";
        constexpr const char* kLastFolderKey = "lastFolder=";
    }

    std::filesystem::path Settings::LoadLastFolder()
    {
        std::ifstream stream{kSettingsFile};
        if (!stream.is_open())
            return {};

        std::string line;
        const std::string prefix = kLastFolderKey;

        while (std::getline(stream, line))
        {
            if (line.rfind(prefix, 0) == 0)
                return std::filesystem::path{line.substr(prefix.size())};
        }

        return {};
    }

    void Settings::SaveLastFolder(const std::filesystem::path& folder)
    {
        std::ofstream stream{kSettingsFile, std::ios::trunc};
        if (!stream.is_open())
        {
            CORE_WARN("Cannot write '{}'", kSettingsFile);
            return;
        }

        stream << kLastFolderKey << folder.string() << '\n';
    }
}
