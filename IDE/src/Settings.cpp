#include "Settings.h"

#include "Core/Base/IniFile.h"
#include "Core/Base/Log.h"
#include "Core/Fs/FileSystem.h"

namespace ide
{
    namespace
    {
        constexpr const char* kSettingsFile = "ide.ini";
        constexpr const char* kLastFolderKey = "lastFolder";
        constexpr const char* kIgnoredDisplayKey = "ignoredDisplay";

        void WriteValue(const char* key, std::string value)
        {
            core::IniFile file = core::IniFile::Load(kSettingsFile);
            file.Set(key, std::move(value));

            if (!file.Save(kSettingsFile))
                CORE_WARN("Cannot write '{}'", kSettingsFile);
        }
    }

    std::filesystem::path Settings::LoadLastFolder()
    {
        return core::fs::FromUtf8(core::IniFile::Load(kSettingsFile).Get(kLastFolderKey));
    }

    void Settings::SaveLastFolder(const std::filesystem::path& folder)
    {
        WriteValue(kLastFolderKey, core::fs::ToUtf8(folder));
    }

    std::string Settings::LoadIgnoredDisplay()
    {
        return core::IniFile::Load(kSettingsFile).Get(kIgnoredDisplayKey);
    }

    void Settings::SaveIgnoredDisplay(std::string_view mode)
    {
        WriteValue(kIgnoredDisplayKey, std::string{mode});
    }
}
