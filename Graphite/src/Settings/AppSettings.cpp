#include "Settings/AppSettings.h"

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

namespace Graphite {

	namespace {
		constexpr const char* kLastFolderKey = "lastFolder";

		constexpr const char* kIgnoredDisplayKey = "ignoredDisplay";

		constexpr const char* kEditorFontSizeKey = "editorFontSize";
		constexpr const char* kIndentWithSpacesKey = "indentWithSpaces";
		constexpr const char* kIndentWidthKey = "indentWidth";
		constexpr const char* kWordWrapKey = "wordWrap";
		constexpr const char* kColorSchemeKey = "colorScheme";
		constexpr const char* kLanguageServerBinaryKey = "languageServerBinary";

		constexpr const char* kSearchRespectGitIgnoreKey = "searchRespectGitIgnore";
		constexpr const char* kSearchProjectsOnlyKey = "searchProjectsOnly";

		constexpr std::string_view kKeyBindingPrefix = "key.";

		const std::filesystem::path& SettingsPath()
		{
			static const std::filesystem::path path = [] {
				const auto directory = Core::ExecutableDirectory();

				if (directory)
					return directory.GetValue() / "graphite.json";

				APP_WARN("Cannot locate the executable directory, using 'graphite.json' as-is: {}",
						 directory.GetError().Message);

				return std::filesystem::path{"graphite.json"};
			}();

			return path;
		}

		float ParseFontSizeOrZero(const std::string& stored)
		{
			char* end = nullptr;
			const float size = std::strtof(stored.c_str(), &end);

			// strtof leaves end at the start of the string when it parsed nothing.
			if (end == stored.c_str())
				return 0.0f;

			return size;
		}

		std::size_t ParseIndentWidthOrZero(const std::string& stored)
		{
			char* end = nullptr;
			const unsigned long width = std::strtoul(stored.c_str(), &end, 10);

			// strtoul leaves end at the start of the string when it parsed nothing.
			if (end == stored.c_str())
				return 0;

			return static_cast<std::size_t>(width);
		}

		void RemoveStoredKeyBindings(Core::JsonFile& file)
		{
			std::vector<std::string> stored;

			for (const std::string& key : file.GetKeys())
			{
				if (std::string_view{key}.starts_with(kKeyBindingPrefix))
				{
					stored.push_back(key);
				}
			}

			for (const std::string& key : stored)
			{
				file.Remove(key);
			}
		}

		void WriteValue(const char* key, std::string value)
		{
			Core::JsonFile file = Core::JsonFile::Load(SettingsPath());
			file.Set(key, std::move(value));

			if (!file.Save(SettingsPath()))
				APP_WARN("Cannot write '{}'", SettingsPath().string());
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
		return ParseFontSizeOrZero(Core::JsonFile::Load(SettingsPath()).Get(kEditorFontSizeKey));
	}

	void AppSettings::SaveEditorFontSize(float size)
	{
		WriteValue(kEditorFontSizeKey, std::to_string(size));
	}

	bool AppSettings::LoadIndentWithSpaces()
	{
		return Core::JsonFile::Load(SettingsPath()).Get(kIndentWithSpacesKey) != "0";
	}

	void AppSettings::SaveIndentWithSpaces(bool useSpaces)
	{
		WriteValue(kIndentWithSpacesKey, useSpaces ? "1" : "0");
	}

	std::size_t AppSettings::LoadIndentWidth()
	{
		return ParseIndentWidthOrZero(Core::JsonFile::Load(SettingsPath()).Get(kIndentWidthKey));
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

	bool AppSettings::LoadSearchRespectGitIgnore()
	{
		return Core::JsonFile::Load(SettingsPath()).Get(kSearchRespectGitIgnoreKey) != "0";
	}

	void AppSettings::SaveSearchRespectGitIgnore(bool respect)
	{
		WriteValue(kSearchRespectGitIgnoreKey, respect ? "1" : "0");
	}

	bool AppSettings::LoadSearchProjectsOnly()
	{
		return Core::JsonFile::Load(SettingsPath()).Get(kSearchProjectsOnlyKey) == "1";
	}

	void AppSettings::SaveSearchProjectsOnly(bool projectsOnly)
	{
		WriteValue(kSearchProjectsOnlyKey, projectsOnly ? "1" : "0");
	}

	std::string AppSettings::LoadColorScheme()
	{
		return Core::JsonFile::Load(SettingsPath()).Get(kColorSchemeKey);
	}

	void AppSettings::SaveColorScheme(std::string_view name)
	{
		WriteValue(kColorSchemeKey, std::string{name});
	}

	std::filesystem::path AppSettings::LoadLanguageServerBinary()
	{
		// Through FromUtf8, like the folder above it: the settings file is UTF-8 and a path on
		// this platform is not.
		return Core::FromUtf8(Core::JsonFile::Load(SettingsPath()).Get(kLanguageServerBinaryKey));
	}

	void AppSettings::SaveLanguageServerBinary(const std::filesystem::path& binary)
	{
		WriteValue(kLanguageServerBinaryKey, Core::ToUtf8(binary));
	}

	std::filesystem::path AppSettings::ThemesDirectory()
	{
		const auto directory = Core::ExecutableDirectory();

		if (directory)
			return directory.GetValue() / "themes";

		APP_WARN("Cannot locate the executable directory, using 'themes' as-is: {}",
				 directory.GetError().Message);

		return std::filesystem::path{"themes"};
	}

	void AppSettings::LoadKeyBindings(Core::Keymap& keys)
	{
		const Core::JsonFile file = Core::JsonFile::Load(SettingsPath());

		for (const std::string& key : file.GetKeys())
		{
			if (!std::string_view{key}.starts_with(kKeyBindingPrefix))
			{
				continue;
			}

			const std::string id = key.substr(kKeyBindingPrefix.size());
			const std::optional<Core::Binding> binding = Core::ParseBinding(file.Get(key));

			if (!binding.has_value())
			{
				APP_WARN("Ignoring the unreadable key binding stored for '{}'", id);
				continue;
			}

			keys.Override(id, *binding);
		}
	}

	void AppSettings::SaveKeyBindings(const Core::Keymap& keys)
	{
		Core::JsonFile file = Core::JsonFile::Load(SettingsPath());

		RemoveStoredKeyBindings(file);

		keys.ForEachOverride(
			[&file](const std::string& id, const Core::Binding& binding) { file.Set(std::string{kKeyBindingPrefix} + id, Core::ToString(binding)); });

		if (!file.Save(SettingsPath()))
		{
			APP_WARN("Cannot write '{}'", SettingsPath().string());
		}
	}
}