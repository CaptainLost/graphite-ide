#include "Settings.h"

#include "Core/Base/IniFile.h"
#include "Core/Base/Log.h"
#include "Core/Fs/FileSystem.h"
#include "Core/Platform/Process.h"

#include <filesystem>

namespace ide
{
    namespace
    {
        constexpr const char* kLastFolderKey = "lastFolder";
        constexpr const char* kIgnoredDisplayKey = "ignoredDisplay";

        /// Resolved once: the executable does not move while it runs, and a settings
        /// write happens on every menu interaction.
        const std::filesystem::path& SettingsPath()
        {
            static const std::filesystem::path path = []
            {
                const auto directory = core::platform::ExecutableDirectory();

                if (directory)
                    return directory.Value() / "ide.ini";

                // Falling back to the working directory keeps settings working rather
                // than losing them, which is the better failure for a preferences file.
                CORE_WARN("Cannot locate the executable directory, using 'ide.ini' as-is: {}",
                          directory.GetError().message);

                return std::filesystem::path{"ide.ini"};
            }();

            return path;
        }

        void WriteValue(const char* key, std::string value)
        {
            core::IniFile file = core::IniFile::Load(SettingsPath());
            file.Set(key, std::move(value));

            if (!file.Save(SettingsPath()))
                CORE_WARN("Cannot write '{}'", SettingsPath().string());
        }
    }

    std::filesystem::path Settings::LoadLastFolder()
    {
        return core::fs::FromUtf8(core::IniFile::Load(SettingsPath()).Get(kLastFolderKey));
    }

    void Settings::SaveLastFolder(const std::filesystem::path& folder)
    {
        WriteValue(kLastFolderKey, core::fs::ToUtf8(folder));
    }

    std::string Settings::LoadIgnoredDisplay()
    {
        return core::IniFile::Load(SettingsPath()).Get(kIgnoredDisplayKey);
    }

    void Settings::SaveIgnoredDisplay(std::string_view mode)
    {
        WriteValue(kIgnoredDisplayKey, std::string{mode});
    }
}
