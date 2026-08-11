#include "AppSettings.h"

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

        /// Still spelled without "git", unlike the GitIgnoredDisplay it stores: this is
        /// the key in a file already written on every machine that has run this build,
        /// and renaming it would silently reset that setting for every one of them.
        constexpr const char* kIgnoredDisplayKey = "ignoredDisplay";

        constexpr const char* kEditorFontSizeKey = "editorFontSize";
        constexpr const char* kIndentWithSpacesKey = "indentWithSpaces";
        constexpr const char* kIndentWidthKey = "indentWidth";
        constexpr const char* kWordWrapKey = "wordWrap";

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

    std::filesystem::path AppSettings::LoadLastFolder()
    {
        return core::fs::FromUtf8(core::JsonFile::Load(SettingsPath()).Get(kLastFolderKey));
    }

    void AppSettings::SaveLastFolder(const std::filesystem::path& folder)
    {
        WriteValue(kLastFolderKey, core::fs::ToUtf8(folder));
    }

    std::string AppSettings::LoadGitIgnoredDisplay()
    {
        return core::JsonFile::Load(SettingsPath()).Get(kIgnoredDisplayKey);
    }

    void AppSettings::SaveGitIgnoredDisplay(std::string_view mode)
    {
        WriteValue(kIgnoredDisplayKey, std::string{mode});
    }

    float AppSettings::LoadEditorFontSize()
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

    void AppSettings::SaveEditorFontSize(float size)
    {
        WriteValue(kEditorFontSizeKey, std::to_string(size));
    }

    bool AppSettings::LoadIndentWithSpaces()
    {
        // Absent or anything other than an explicit "0" reads as spaces, which is also
        // IndentStyle's own default — a hand-edited file cannot land on a value this
        // does not recognise.
        return core::JsonFile::Load(SettingsPath()).Get(kIndentWithSpacesKey) != "0";
    }

    void AppSettings::SaveIndentWithSpaces(bool useSpaces)
    {
        WriteValue(kIndentWithSpacesKey, useSpaces ? "1" : "0");
    }

    std::size_t AppSettings::LoadIndentWidth()
    {
        const std::string stored = core::JsonFile::Load(SettingsPath()).Get(kIndentWidthKey);

        char* end = nullptr;
        const unsigned long width = std::strtoul(stored.c_str(), &end, 10);

        // An unparseable, empty or zero value reads as "nothing stored", so the caller's
        // default applies rather than an unusable indent width.
        if (end == stored.c_str() || width == 0)
            return 0;

        return static_cast<std::size_t>(width);
    }

    void AppSettings::SaveIndentWidth(std::size_t width)
    {
        WriteValue(kIndentWidthKey, std::to_string(width));
    }

    bool AppSettings::LoadWordWrap()
    {
        return core::JsonFile::Load(SettingsPath()).Get(kWordWrapKey) == "1";
    }

    void AppSettings::SaveWordWrap(bool wrap)
    {
        WriteValue(kWordWrapKey, wrap ? "1" : "0");
    }
}
