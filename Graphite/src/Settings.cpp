#include "Settings.h"

#include "Core/Base/JsonFile.h"
#include "Core/Base/Log.h"
#include "Core/Fs/FileSystem.h"
#include "Core/Platform/Process.h"

#include <cstdlib>
#include <filesystem>

namespace graphite
{
    namespace
    {
        constexpr const char* kLastFolderKey = "lastFolder";
        constexpr const char* kIgnoredDisplayKey = "ignoredDisplay";
        constexpr const char* kEditorFontSizeKey = "editorFontSize";

        /// Resolved once: the executable does not move while it runs, and a settings
        /// write happens on every menu interaction.
        const std::filesystem::path& SettingsPath()
        {
            static const std::filesystem::path path = []
            {
                const auto directory = core::platform::ExecutableDirectory();

                if (directory)
                    return directory.Value() / "graphite.json";

                // Falling back to the working directory keeps settings working rather
                // than losing them, which is the better failure for a preferences file.
                CORE_WARN("Cannot locate the executable directory, using 'graphite.json' as-is: {}",
                          directory.GetError().message);

                return std::filesystem::path{"graphite.json"};
            }();

            return path;
        }

        void WriteValue(const char* key, std::string value)
        {
            core::JsonFile file = core::JsonFile::Load(SettingsPath());
            file.Set(key, std::move(value));

            if (!file.Save(SettingsPath()))
                CORE_WARN("Cannot write '{}'", SettingsPath().string());
        }
    }

    std::filesystem::path Settings::LoadLastFolder()
    {
        return core::fs::FromUtf8(core::JsonFile::Load(SettingsPath()).Get(kLastFolderKey));
    }

    void Settings::SaveLastFolder(const std::filesystem::path& folder)
    {
        WriteValue(kLastFolderKey, core::fs::ToUtf8(folder));
    }

    std::string Settings::LoadIgnoredDisplay()
    {
        return core::JsonFile::Load(SettingsPath()).Get(kIgnoredDisplayKey);
    }

    void Settings::SaveIgnoredDisplay(std::string_view mode)
    {
        WriteValue(kIgnoredDisplayKey, std::string{mode});
    }

    float Settings::LoadEditorFontSize()
    {
        const std::string stored = core::JsonFile::Load(SettingsPath()).Get(kEditorFontSizeKey);

        char* end = nullptr;
        const float size = std::strtof(stored.c_str(), &end);

        // An unparseable or empty value (strtof leaves end == stored.data()) reads as
        // "nothing stored", so the caller's default applies rather than a garbage size.
        if (end == stored.c_str())
            return 0.0f;

        return size;
    }

    void Settings::SaveEditorFontSize(float size)
    {
        WriteValue(kEditorFontSizeKey, std::to_string(size));
    }
}
