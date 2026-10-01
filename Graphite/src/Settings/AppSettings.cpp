#include "Settings/AppSettings.h"

#include "Settings/InterfaceScale.h"
#include "Settings/SettingsVersion.h"
#include "Settings/UserDirectories.h"

#include "Persistence/StateCodec.h"

#include "Core/Base/JsonFile.h"
#include "Core/Base/Log.h"
#include "Core/Command/Binding.h"
#include "Core/Command/Keymap.h"
#include "Core/Fs/FileSystem.h"

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace Graphite {

	namespace {
		constexpr const char* kLastFolderKey = "lastFolder";

		constexpr const char* kIgnoredDisplayKey = "ignoredDisplay";

		constexpr const char* kInterfaceScaleKey = "interfaceScale";
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

		// Loads graphite.json, lets `edit` change it and saves it back through SaveSettings.
		template <typename Edit>
		void ModifySettings(Edit&& edit)
		{
			Core::JsonFile file = Core::JsonFile::Load(SettingsPath());
			edit(file);
			SaveSettings(file);
		}

		void WriteValue(std::string_view key, std::string value)
		{
			ModifySettings([&](Core::JsonFile& file) { file.Set(key, std::move(value)); });
		}

		std::string ReadValue(std::string_view key)
		{
			return Core::JsonFile::Load(SettingsPath()).Get(key);
		}

		// Zero is "absent" for every number stored here, so a malformed one reads as absent too.
		template <typename Number>
		Number ReadNumberOrZero(std::string_view key)
		{
			return ParseNumber<Number>(ReadValue(key)).value_or(Number{});
		}

		bool ReadStoredFlag(std::string_view key, bool fallback)
		{
			return ReadFlag(ReadValue(key), fallback);
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

	int AppSettings::LoadInterfaceScalePercent()
	{
		return InterfaceScalePercentOrDefault(
			Core::JsonFile::Load(SettingsPath()).Get(kInterfaceScaleKey));
	}

	void AppSettings::SaveInterfaceScalePercent(int percent)
	{
		WriteValue(kInterfaceScaleKey, std::to_string(percent));
	}

	float AppSettings::LoadEditorFontSize()
	{
		return ReadNumberOrZero<float>(kEditorFontSizeKey);
	}

	void AppSettings::SaveEditorFontSize(float size)
	{
		WriteValue(kEditorFontSizeKey, std::to_string(size));
	}

	bool AppSettings::LoadIndentWithSpaces()
	{
		return ReadStoredFlag(kIndentWithSpacesKey, true);
	}

	void AppSettings::SaveIndentWithSpaces(bool useSpaces)
	{
		WriteValue(kIndentWithSpacesKey, Flag(useSpaces));
	}

	std::size_t AppSettings::LoadIndentWidth()
	{
		return ReadNumberOrZero<std::size_t>(kIndentWidthKey);
	}

	void AppSettings::SaveIndentWidth(std::size_t width)
	{
		WriteValue(kIndentWidthKey, std::to_string(width));
	}

	bool AppSettings::LoadWordWrap()
	{
		return ReadStoredFlag(kWordWrapKey, false);
	}

	void AppSettings::SaveWordWrap(bool wrap)
	{
		WriteValue(kWordWrapKey, Flag(wrap));
	}

	// Both default on: a file that predates the switches, and so has neither key, keeps showing
	// the markers it always showed.
	bool AppSettings::LoadGutterChangeMarkers()
	{
		return ReadStoredFlag(kGutterChangeMarkersKey, true);
	}

	void AppSettings::SaveGutterChangeMarkers(bool show)
	{
		WriteValue(kGutterChangeMarkersKey, Flag(show));
	}

	bool AppSettings::LoadScrollbarChangeMarkers()
	{
		return ReadStoredFlag(kScrollbarChangeMarkersKey, true);
	}

	void AppSettings::SaveScrollbarChangeMarkers(bool show)
	{
		WriteValue(kScrollbarChangeMarkersKey, Flag(show));
	}

	bool AppSettings::LoadSearchRespectGitIgnore()
	{
		return ReadStoredFlag(kSearchRespectGitIgnoreKey, true);
	}

	void AppSettings::SaveSearchRespectGitIgnore(bool respect)
	{
		WriteValue(kSearchRespectGitIgnoreKey, Flag(respect));
	}

	bool AppSettings::LoadSearchProjectsOnly()
	{
		return ReadStoredFlag(kSearchProjectsOnlyKey, false);
	}

	void AppSettings::SaveSearchProjectsOnly(bool projectsOnly)
	{
		WriteValue(kSearchProjectsOnlyKey, Flag(projectsOnly));
	}

	bool AppSettings::LoadBackgroundAnalysis()
	{
		// Absent means on: the feature is the default, and a settings file written before it
		// existed must not read as a decision to switch it off.
		return ReadStoredFlag(kBackgroundAnalysisKey, true);
	}

	void AppSettings::SaveBackgroundAnalysis(bool enabled)
	{
		WriteValue(kBackgroundAnalysisKey, Flag(enabled));
	}

	std::size_t AppSettings::LoadAnalysisBudgetMegabytes()
	{
		return ReadNumberOrZero<std::size_t>(kAnalysisBudgetKey);
	}

	void AppSettings::SaveAnalysisBudgetMegabytes(std::size_t megabytes)
	{
		WriteValue(kAnalysisBudgetKey, std::to_string(megabytes));
	}

	std::size_t AppSettings::LoadAnalysisConcurrency()
	{
		return ReadNumberOrZero<std::size_t>(kAnalysisConcurrencyKey);
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
		WriteValue(std::string{kClaudePricePrefix} + std::string{model}, std::string{price});
	}

	void AppSettings::RemoveClaudePrice(std::string_view model)
	{
		ModifySettings([&](Core::JsonFile& file) { file.Remove(std::string{kClaudePricePrefix} + std::string{model}); });
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
		ModifySettings([&keys](Core::JsonFile& file) {
			RemoveKeysStartingWith(file, kKeyBindingPrefix);

			keys.ForEachOverride([&file](const std::string& id, const Core::Binding& binding) {
				file.Set(std::string{kKeyBindingPrefix} + id, Core::ToString(binding));
			});
		});
	}

	GlobalState AppSettings::LoadGlobalState()
	{
		return ReadGlobalState(Core::JsonFile::Load(SettingsPath()));
	}

	void AppSettings::SaveGlobalState(const GlobalState& state)
	{
		ModifySettings([&state](Core::JsonFile& file) { WriteGlobalState(state, file); });
	}
}
