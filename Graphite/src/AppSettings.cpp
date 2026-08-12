#include "AppSettings.h"

#include "Core/Base/JsonFile.h"
#include "Core/Base/Log.h"
#include "Core/Command/Binding.h"
#include "Core/Command/Keymap.h"
#include "Core/Fs/FileSystem.h"
#include "Core/Platform/Process.h"

#include <cstdlib>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace Graphite
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

        /// One key per overridden command, `key.` plus the command id. Flat, like every
        /// other setting here: a blob under a single key would not be hand-editable, and
        /// this file is meant to be.
        constexpr std::string_view kKeyBindingPrefix = "key.";

        /// Resolved once: the executable does not move while it runs, and a settings
        /// write happens on every menu interaction.
        const std::filesystem::path& SettingsPath()
        {
            static const std::filesystem::path path = []
            {
                const auto directory = Core::ExecutableDirectory();

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
            Core::JsonFile file = Core::JsonFile::Load(SettingsPath());
            file.Set(key, std::move(value));

            if (!file.Save(SettingsPath()))
                CORE_WARN("Cannot write '{}'", SettingsPath().string());
        }
    }

    std::filesystem::path AppSettings::LoadLastFolder()
    {
        return Core::FromUtf8(Core::JsonFile::Load(SettingsPath()).Get(kLastFolderKey));
    }

    void AppSettings::SaveLastFolder(const std::filesystem::path& folder)
    {
        WriteValue(kLastFolderKey, Core::ToUtf8(folder));
    }

    std::string AppSettings::LoadGitIgnoredDisplay()
    {
        return Core::JsonFile::Load(SettingsPath()).Get(kIgnoredDisplayKey);
    }

    void AppSettings::SaveGitIgnoredDisplay(std::string_view mode)
    {
        WriteValue(kIgnoredDisplayKey, std::string{mode});
    }

    float AppSettings::LoadEditorFontSize()
    {
        const std::string stored = Core::JsonFile::Load(SettingsPath()).Get(kEditorFontSizeKey);

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
        return Core::JsonFile::Load(SettingsPath()).Get(kIndentWithSpacesKey) != "0";
    }

    void AppSettings::SaveIndentWithSpaces(bool useSpaces)
    {
        WriteValue(kIndentWithSpacesKey, useSpaces ? "1" : "0");
    }

    std::size_t AppSettings::LoadIndentWidth()
    {
        const std::string stored = Core::JsonFile::Load(SettingsPath()).Get(kIndentWidthKey);

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
        return Core::JsonFile::Load(SettingsPath()).Get(kWordWrapKey) == "1";
    }

    void AppSettings::SaveWordWrap(bool wrap)
    {
        WriteValue(kWordWrapKey, wrap ? "1" : "0");
    }

    void AppSettings::LoadKeyBindings(Core::Keymap& keys)
    {
        const Core::JsonFile file = Core::JsonFile::Load(SettingsPath());

        for (const std::string& key : file.Keys())
        {
            if (!std::string_view{key}.starts_with(kKeyBindingPrefix))
            {
                continue;
            }

            const std::string id = key.substr(kKeyBindingPrefix.size());
            const std::optional<Core::Binding> binding = Core::ParseBinding(file.Get(key));

            // Skipped rather than fatal, and named so it can be found: one hand-edited
            // line should cost the one binding it describes, not every shortcut in the
            // IDE.
            if (!binding.has_value())
            {
                CORE_WARN("Ignoring the unreadable key binding stored for '{}'", id);
                continue;
            }

            keys.Override(id, *binding);
        }
    }

    void AppSettings::SaveKeyBindings(const Core::Keymap& keys)
    {
        Core::JsonFile file = Core::JsonFile::Load(SettingsPath());

        // Written in one pass rather than through WriteValue per command: that helper
        // loads and saves the whole file each time, which over a table of bindings would
        // rewrite the file once per entry.
        //
        // Collected before removing, because removing while iterating the keys would
        // invalidate what is being walked.
        std::vector<std::string> stale;

        for (const std::string& key : file.Keys())
        {
            if (std::string_view{key}.starts_with(kKeyBindingPrefix))
            {
                stale.push_back(key);
            }
        }

        // Removed, not blanked: a binding the user has reset must leave the file rather
        // than sit there as an empty value that LoadKeyBindings would warn about on every
        // start.
        for (const std::string& key : stale)
        {
            file.Remove(key);
        }

        keys.ForEachOverride(
            [&file](const std::string& id, const Core::Binding& binding)
            { file.Set(std::string{kKeyBindingPrefix} + id, Core::ToString(binding)); });

        if (!file.Save(SettingsPath()))
        {
            CORE_WARN("Cannot write '{}'", SettingsPath().string());
        }
    }
}
