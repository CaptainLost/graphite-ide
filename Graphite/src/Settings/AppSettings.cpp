#include "Settings/AppSettings.h"

#include "Settings/SettingsVersion.h"
#include "Settings/UserDirectories.h"

#include "Persistence/StateCodec.h"

#include "Core/Base/JsonFile.h"
#include "Core/Base/Log.h"
#include "Core/Command/Binding.h"
#include "Core/Command/Keymap.h"
#include "Core/Fs/FileSystem.h"

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
		constexpr const char* kGutterChangeMarkersKey = "gutterChangeMarkers";
		constexpr const char* kScrollbarChangeMarkersKey = "scrollbarChangeMarkers";
		constexpr const char* kColorSchemeKey = "colorScheme";
		constexpr const char* kLanguageServerBinaryKey = "languageServerBinary";
		constexpr const char* kClaudeBinaryKey = "claude.binary";
		constexpr std::string_view kClaudePricePrefix = "claude.price.";

		constexpr const char* kSearchRespectGitIgnoreKey = "searchRespectGitIgnore";
		constexpr const char* kSearchProjectsOnlyKey = "searchProjectsOnly";

		constexpr const char* kBackgroundAnalysisKey = "backgroundAnalysis";
		constexpr const char* kAnalysisBudgetKey = "analysisBudgetMegabytes";
		constexpr const char* kAnalysisConcurrencyKey = "analysisConcurrency";

		constexpr std::string_view kKeyBindingPrefix = "key.";

		const std::filesystem::path& SettingsPath()
		{
			static const std::filesystem::path path = UserDirectories::SettingsFile();
			return path;
		}

		// Every write of graphite.json goes through here, so none of them can skip the
		// version check and overwrite a file a newer build wrote.
		void SaveSettings(Core::JsonFile& file)
		{
			if (!StampSettingsVersion(file))
			{
				APP_WARN("Not writing '{}': a newer version of Graphite wrote it", Core::ToUtf8(SettingsPath()));
				return;
			}

			if (!file.Save(SettingsPath()))
			{
				APP_WARN("Cannot write '{}'", Core::ToUtf8(SettingsPath()));
			}
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

		// Shared by every stored value that is "a small non-negative number, or absent" —
		// the analysis budget and the analysis concurrency both read this way, and zero
		// already means "absent" or "automatic" for each of them.
		std::size_t ParseSizeOrZero(const std::string& stored)
		{
			char* end = nullptr;
			const unsigned long value = std::strtoul(stored.c_str(), &end, 10);

			// strtoul leaves end at the start of the string when it parsed nothing.
			if (end == stored.c_str())
			{
				return 0;
			}

			return static_cast<std::size_t>(value);
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
			SaveSettings(file);
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

	// Both default on: a file that predates the switches, and so has neither key, keeps showing
	// the markers it always showed.
	bool AppSettings::LoadGutterChangeMarkers()
	{
		return Core::JsonFile::Load(SettingsPath()).Get(kGutterChangeMarkersKey) != "0";
	}

	void AppSettings::SaveGutterChangeMarkers(bool show)
	{
		WriteValue(kGutterChangeMarkersKey, show ? "1" : "0");
	}

	bool AppSettings::LoadScrollbarChangeMarkers()
	{
		return Core::JsonFile::Load(SettingsPath()).Get(kScrollbarChangeMarkersKey) != "0";
	}

	void AppSettings::SaveScrollbarChangeMarkers(bool show)
	{
		WriteValue(kScrollbarChangeMarkersKey, show ? "1" : "0");
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

	bool AppSettings::LoadBackgroundAnalysis()
	{
		// Absent means on, which is why this compares against "0" rather than for "1": the
		// feature is the default, and a settings file written before it existed must not read as
		// a decision to switch it off.
		return Core::JsonFile::Load(SettingsPath()).Get(kBackgroundAnalysisKey) != "0";
	}

	void AppSettings::SaveBackgroundAnalysis(bool enabled)
	{
		WriteValue(kBackgroundAnalysisKey, enabled ? "1" : "0");
	}

	std::size_t AppSettings::LoadAnalysisBudgetMegabytes()
	{
		return ParseSizeOrZero(Core::JsonFile::Load(SettingsPath()).Get(kAnalysisBudgetKey));
	}

	void AppSettings::SaveAnalysisBudgetMegabytes(std::size_t megabytes)
	{
		WriteValue(kAnalysisBudgetKey, std::to_string(megabytes));
	}

	std::size_t AppSettings::LoadAnalysisConcurrency()
	{
		return ParseSizeOrZero(Core::JsonFile::Load(SettingsPath()).Get(kAnalysisConcurrencyKey));
	}

	void AppSettings::SaveAnalysisConcurrency(std::size_t concurrency)
	{
		WriteValue(kAnalysisConcurrencyKey, std::to_string(concurrency));
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

	std::filesystem::path AppSettings::LoadClaudeBinary()
	{
		return Core::FromUtf8(Core::JsonFile::Load(SettingsPath()).Get(kClaudeBinaryKey));
	}

	void AppSettings::SaveClaudeBinary(const std::filesystem::path& binary)
	{
		WriteValue(kClaudeBinaryKey, Core::ToUtf8(binary));
	}

	std::vector<std::pair<std::string, std::string>> AppSettings::LoadClaudePrices()
	{
		const Core::JsonFile file = Core::JsonFile::Load(SettingsPath());
		std::vector<std::pair<std::string, std::string>> prices;

		for (const std::string& key : file.GetKeys())
		{
			if (!std::string_view{key}.starts_with(kClaudePricePrefix))
				continue;

			prices.emplace_back(key.substr(kClaudePricePrefix.size()), file.Get(key));
		}

		return prices;
	}

	void AppSettings::SaveClaudePrice(std::string_view model, std::string_view price)
	{
		WriteValue((std::string{kClaudePricePrefix} + std::string{model}).c_str(), std::string{price});
	}

	void AppSettings::RemoveClaudePrice(std::string_view model)
	{
		Core::JsonFile file = Core::JsonFile::Load(SettingsPath());
		file.Remove(std::string{kClaudePricePrefix} + std::string{model});
		SaveSettings(file);
	}

	std::filesystem::path AppSettings::ThemesDirectory()
	{
		return UserDirectories::ThemesDirectory();
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

		SaveSettings(file);
	}

	GlobalState AppSettings::LoadGlobalState()
	{
		return ReadGlobalState(Core::JsonFile::Load(SettingsPath()));
	}

	void AppSettings::SaveGlobalState(const GlobalState& state)
	{
		Core::JsonFile file = Core::JsonFile::Load(SettingsPath());
		WriteGlobalState(state, file);

		SaveSettings(file);
	}
}
