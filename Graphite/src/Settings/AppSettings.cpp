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
#include <system_error>
#include <utility>
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

		// Zero is "absent" for every number stored here, so a malformed one reads as absent too.
		template<typename Number>
		Number NumberOrZero(std::string_view stored)
		{
			return ParseNumber<Number>(stored).value_or(Number{});
		}
	}

	AppSettings::AppSettings(std::filesystem::path file)
		: m_File(std::move(file)), m_Document(Core::JsonFile::Load(m_File))
	{
		m_Written = TakeSnapshot(m_Document);
	}

	void AppSettings::Flush()
	{
		if (!m_Unwritten)
		{
			return;
		}

		Core::JsonFile onDisk = Core::JsonFile::Load(m_File);

		// An empty read of a file that has bytes in it is a file that does not parse. There is
		// nothing on disk to keep, so the copy in memory is written, as it always was.
		std::error_code error;
		const bool unreadable = onDisk.GetCount() == 0 && std::filesystem::file_size(m_File, error) > 0 && !error;

		if (!unreadable)
		{
			// Only the keys this process changed since the last write go over what is on disk, so
			// everything else there - a hand edit, another instance's change - is kept.
			const Snapshot current = TakeSnapshot(m_Document);

			for (const auto& [key, value] : current)
			{
				if (const auto written = m_Written.find(key); written == m_Written.end() || written->second != value)
				{
					onDisk.Set(key, value);
				}
			}

			for (const auto& [key, value] : m_Written)
			{
				if (!current.contains(key))
				{
					onDisk.Remove(key);
				}
			}
		}

		Core::JsonFile& toWrite = unreadable ? m_Document : onDisk;

		// Every write of graphite.json goes through here, so none of them can skip the
		// version check and overwrite a file a newer build wrote. Not retried: the file will not
		// get older by waiting.
		if (!StampSettingsVersion(toWrite))
		{
			m_Unwritten = false;
			APP_WARN("Not writing '{}': a newer version of Graphite wrote it", Core::ToUtf8(m_File));
			return;
		}

		if (!toWrite.Save(m_File))
		{
			// Left unwritten so the next Flush tries again; logged once per run of failures rather
			// than on every frame that retries.
			if (!m_WriteFailed)
			{
				APP_WARN("Cannot write '{}'", Core::ToUtf8(m_File));
			}

			m_WriteFailed = true;
			return;
		}

		// What was written is now what this process holds, hand edits from disk included.
		if (!unreadable)
		{
			m_Document = std::move(onDisk);
		}

		m_Written = TakeSnapshot(m_Document);
		m_Unwritten = false;
		m_WriteFailed = false;
	}

	AppSettings::Snapshot AppSettings::TakeSnapshot(const Core::JsonFile& document)
	{
		Snapshot snapshot;

		for (const std::string& key : document.GetKeys())
		{
			snapshot.emplace(key, document.Get(key));
		}

		return snapshot;
	}

	std::string AppSettings::Get(std::string_view key) const
	{
		return m_Document.Get(key);
	}

	void AppSettings::Set(std::string_view key, std::string value)
	{
		m_Document.Set(key, std::move(value));
		m_Unwritten = true;
	}

	std::filesystem::path AppSettings::GetLastFolder() const
	{
		return Core::FromUtf8(Get(kLastFolderKey));
	}

	void AppSettings::SetLastFolder(const std::filesystem::path& folder)
	{
		Set(kLastFolderKey, Core::ToUtf8(folder));
	}

	std::string AppSettings::GetGitIgnoredDisplay() const
	{
		return Get(kIgnoredDisplayKey);
	}

	void AppSettings::SetGitIgnoredDisplay(std::string_view mode)
	{
		Set(kIgnoredDisplayKey, std::string{mode});
	}

	int AppSettings::GetInterfaceScalePercent() const
	{
		return InterfaceScalePercentOrDefault(Get(kInterfaceScaleKey));
	}

	void AppSettings::SetInterfaceScalePercent(int percent)
	{
		Set(kInterfaceScaleKey, std::to_string(percent));
	}

	float AppSettings::GetEditorFontSize() const
	{
		return NumberOrZero<float>(Get(kEditorFontSizeKey));
	}

	void AppSettings::SetEditorFontSize(float size)
	{
		Set(kEditorFontSizeKey, std::to_string(size));
	}

	bool AppSettings::GetIndentWithSpaces() const
	{
		return ReadFlag(Get(kIndentWithSpacesKey), true);
	}

	void AppSettings::SetIndentWithSpaces(bool useSpaces)
	{
		Set(kIndentWithSpacesKey, Flag(useSpaces));
	}

	std::size_t AppSettings::GetIndentWidth() const
	{
		return NumberOrZero<std::size_t>(Get(kIndentWidthKey));
	}

	void AppSettings::SetIndentWidth(std::size_t width)
	{
		Set(kIndentWidthKey, std::to_string(width));
	}

	bool AppSettings::GetWordWrap() const
	{
		return ReadFlag(Get(kWordWrapKey), false);
	}

	void AppSettings::SetWordWrap(bool wrap)
	{
		Set(kWordWrapKey, Flag(wrap));
	}

	// Both default on: a file that predates the switches, and so has neither key, keeps showing
	// the markers it always showed.
	bool AppSettings::GetGutterChangeMarkers() const
	{
		return ReadFlag(Get(kGutterChangeMarkersKey), true);
	}

	void AppSettings::SetGutterChangeMarkers(bool show)
	{
		Set(kGutterChangeMarkersKey, Flag(show));
	}

	bool AppSettings::GetScrollbarChangeMarkers() const
	{
		return ReadFlag(Get(kScrollbarChangeMarkersKey), true);
	}

	void AppSettings::SetScrollbarChangeMarkers(bool show)
	{
		Set(kScrollbarChangeMarkersKey, Flag(show));
	}

	bool AppSettings::GetSearchRespectGitIgnore() const
	{
		return ReadFlag(Get(kSearchRespectGitIgnoreKey), true);
	}

	void AppSettings::SetSearchRespectGitIgnore(bool respect)
	{
		Set(kSearchRespectGitIgnoreKey, Flag(respect));
	}

	bool AppSettings::GetSearchProjectsOnly() const
	{
		return ReadFlag(Get(kSearchProjectsOnlyKey), false);
	}

	void AppSettings::SetSearchProjectsOnly(bool projectsOnly)
	{
		Set(kSearchProjectsOnlyKey, Flag(projectsOnly));
	}

	bool AppSettings::GetBackgroundAnalysis() const
	{
		// Absent means on: the feature is the default, and a settings file written before it
		// existed must not read as a decision to switch it off.
		return ReadFlag(Get(kBackgroundAnalysisKey), true);
	}

	void AppSettings::SetBackgroundAnalysis(bool enabled)
	{
		Set(kBackgroundAnalysisKey, Flag(enabled));
	}

	std::size_t AppSettings::GetAnalysisBudgetMegabytes() const
	{
		return NumberOrZero<std::size_t>(Get(kAnalysisBudgetKey));
	}

	void AppSettings::SetAnalysisBudgetMegabytes(std::size_t megabytes)
	{
		Set(kAnalysisBudgetKey, std::to_string(megabytes));
	}

	std::size_t AppSettings::GetAnalysisConcurrency() const
	{
		return NumberOrZero<std::size_t>(Get(kAnalysisConcurrencyKey));
	}

	void AppSettings::SetAnalysisConcurrency(std::size_t concurrency)
	{
		Set(kAnalysisConcurrencyKey, std::to_string(concurrency));
	}

	std::string AppSettings::GetColorScheme() const
	{
		return Get(kColorSchemeKey);
	}

	void AppSettings::SetColorScheme(std::string_view name)
	{
		Set(kColorSchemeKey, std::string{name});
	}

	std::filesystem::path AppSettings::GetLanguageServerBinary() const
	{
		// Through FromUtf8, like the folder above it: the settings file is UTF-8 and a path on
		// this platform is not.
		return Core::FromUtf8(Get(kLanguageServerBinaryKey));
	}

	void AppSettings::SetLanguageServerBinary(const std::filesystem::path& binary)
	{
		Set(kLanguageServerBinaryKey, Core::ToUtf8(binary));
	}

	std::filesystem::path AppSettings::GetClaudeBinary() const
	{
		return Core::FromUtf8(Get(kClaudeBinaryKey));
	}

	void AppSettings::SetClaudeBinary(const std::filesystem::path& binary)
	{
		Set(kClaudeBinaryKey, Core::ToUtf8(binary));
	}

	std::vector<std::pair<std::string, std::string>> AppSettings::GetClaudePrices() const
	{
		std::vector<std::pair<std::string, std::string>> prices;

		for (const std::string& key : m_Document.GetKeys())
		{
			if (!std::string_view{key}.starts_with(kClaudePricePrefix))
			{
				continue;
			}

			prices.emplace_back(key.substr(kClaudePricePrefix.size()), m_Document.Get(key));
		}

		return prices;
	}

	void AppSettings::SetClaudePrice(std::string_view model, std::string_view price)
	{
		Set(std::string{kClaudePricePrefix} + std::string{model}, std::string{price});
	}

	void AppSettings::RemoveClaudePrice(std::string_view model)
	{
		m_Document.Remove(std::string{kClaudePricePrefix} + std::string{model});
		m_Unwritten = true;
	}

	std::filesystem::path AppSettings::ThemesDirectory()
	{
		return UserDirectories::ThemesDirectory();
	}

	void AppSettings::ApplyKeyBindings(Core::Keymap& keys) const
	{
		for (const std::string& key : m_Document.GetKeys())
		{
			if (!std::string_view{key}.starts_with(kKeyBindingPrefix))
			{
				continue;
			}

			const std::string id = key.substr(kKeyBindingPrefix.size());
			const std::optional<Core::Binding> binding = Core::ParseBinding(m_Document.Get(key));

			if (!binding.has_value())
			{
				APP_WARN("Ignoring the unreadable key binding stored for '{}'", id);
				continue;
			}

			keys.Override(id, *binding);
		}
	}

	void AppSettings::SetKeyBindings(const Core::Keymap& keys)
	{
		RemoveKeysStartingWith(m_Document, kKeyBindingPrefix);

		keys.ForEachOverride([this](const std::string& id, const Core::Binding& binding) {
			m_Document.Set(std::string{kKeyBindingPrefix} + id, Core::ToString(binding));
		});

		m_Unwritten = true;
	}

	GlobalState AppSettings::GetGlobalState() const
	{
		return ReadGlobalState(m_Document);
	}

	void AppSettings::SetGlobalState(const GlobalState& state)
	{
		WriteGlobalState(state, m_Document);
		m_Unwritten = true;
	}
}
