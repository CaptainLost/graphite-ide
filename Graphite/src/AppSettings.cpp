#include "AppSettings.h"

#include "Core/Base/Ascii.h"
#include "Core/Base/JsonFile.h"
#include "Core/Base/Log.h"
#include "Core/Command/Binding.h"
#include "Core/Command/Chord.h"
#include "Core/Command/Keymap.h"
#include "Core/Fs/FileSystem.h"
#include "Core/Platform/Process.h"

#include <cstdlib>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

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

        /// One key per overridden command, `key.` plus the command id. Flat, like every
        /// other setting here: a blob under a single key would not be hand-editable, and
        /// this file is meant to be.
        constexpr std::string_view kKeyBindingPrefix = "key.";

        /// Primary first, alternates after, '|' between. The separator cannot occur in a
        /// chord's own text, which is made of modifier and key names joined by '+'.
        constexpr char kChordSeparator = '|';

        /// What a command the user took every chord away from is written as. The empty
        /// string cannot carry it — Deserialise refuses empty text, so an unbound override
        /// would be dropped on the next start and the factory chord would come back,
        /// silently undoing the very conflict the rebinding screen resolved. No key is
        /// named "None", so this cannot collide with a chord's own text.
        constexpr std::string_view kUnboundText = "None";

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

        [[nodiscard]] std::string Serialise(const core::Binding& binding)
        {
            if (binding.primary.IsEmpty() && binding.alternates.empty())
            {
                return std::string{kUnboundText};
            }

            std::string text = binding.primary.ToString();

            for (const core::Chord& alternate : binding.alternates)
            {
                text += kChordSeparator;
                text += alternate.ToString();
            }

            return text;
        }

        /// Empty when any part of the text names a chord this build does not know, so a
        /// half-understood binding is refused whole rather than applied in part. An empty
        /// text is a malformed entry too, not a binding of nothing.
        [[nodiscard]] std::optional<core::Binding> Deserialise(std::string_view text)
        {
            core::Binding binding;
            bool first = true;
            std::size_t start = 0;

            // An override that binds nothing, which is a different thing from a missing
            // override: the latter restores the factory chord.
            // Case-insensitively, like Chord::Parse reads every other name in this file:
            // a hand-written "none" must not be refused and silently restore the factory
            // chord, which is the one failure this token exists to prevent.
            if (core::ascii::Lowered(core::ascii::Trimmed(text)) == core::ascii::Lowered(kUnboundText))
            {
                return binding;
            }

            for (;;)
            {
                const std::size_t separator = text.find(kChordSeparator, start);
                const std::string_view part =
                    text.substr(start, separator == std::string_view::npos
                                           ? std::string_view::npos
                                           : separator - start);

                const std::optional<core::Chord> chord = core::Chord::Parse(part);

                if (!chord.has_value())
                {
                    return std::nullopt;
                }

                if (first)
                {
                    binding.primary = *chord;
                    first = false;
                }
                else
                {
                    binding.alternates.push_back(*chord);
                }

                if (separator == std::string_view::npos)
                {
                    break;
                }

                start = separator + 1;
            }

            return binding;
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

    void AppSettings::LoadKeyBindings(core::Keymap& keys)
    {
        const core::JsonFile file = core::JsonFile::Load(SettingsPath());

        for (const std::string& key : file.Keys())
        {
            if (!std::string_view{key}.starts_with(kKeyBindingPrefix))
            {
                continue;
            }

            const std::string id = key.substr(kKeyBindingPrefix.size());
            const std::optional<core::Binding> binding = Deserialise(file.Get(key));

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

    void AppSettings::SaveKeyBindings(const core::Keymap& keys)
    {
        core::JsonFile file = core::JsonFile::Load(SettingsPath());

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

        keys.ForEachOverride([&file](const std::string& id, const core::Binding& binding)
                             { file.Set(std::string{kKeyBindingPrefix} + id, Serialise(binding)); });

        if (!file.Save(SettingsPath()))
        {
            CORE_WARN("Cannot write '{}'", SettingsPath().string());
        }
    }
}
