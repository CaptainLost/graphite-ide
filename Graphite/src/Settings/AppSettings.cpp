#include "Settings/AppSettings.h"

#include "Settings/InterfaceScale.h"
#include "Settings/JsonText.h"
#include "Settings/SettingsVersion.h"
#include "Settings/UserDirectories.h"

#include "Persistence/FileBackup.h"
#include "Persistence/StateCodec.h"

#include "Core/Base/Ascii.h"
#include "Core/Base/JsonFile.h"
#include "Core/Base/Log.h"
#include "Core/Base/Strings.h"
#include "Core/Command/Binding.h"
#include "Core/Command/Keymap.h"
#include "Core/FileSystem/FileSystem.h"

#include <nlohmann/json.hpp>

#include <concepts>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace Graphite {

	namespace {
		constexpr const char* kLastFolderKey = "lastFolder";

		constexpr const char* kIgnoredDisplayKey = "ignoredDisplay";
		constexpr const char* kExplorerFollowsActiveFileKey = "explorerFollowsActiveFile";

		constexpr const char* kInterfaceScaleKey = "interfaceScale";
		constexpr const char* kEditorFontSizeKey = "editorFontSize";
		constexpr const char* kIndentWithSpacesKey = "indentWithSpaces";
		constexpr const char* kIndentWidthKey = "indentWidth";
		constexpr const char* kWordWrapKey = "wordWrap";
		constexpr const char* kDiffWordWrapKey = "diffWordWrap";
		constexpr const char* kDiffLayoutKey = "diffLayout";
		constexpr const char* kImageBackgroundKey = "imageBackground";
		constexpr const char* kDiffScopeKey = "diffScope";
		constexpr const char* kDiffColourSyntaxKey = "diffColourSyntax";
		constexpr const char* kGutterChangeMarkersKey = "gutterChangeMarkers";
		constexpr const char* kScrollbarChangeMarkersKey = "scrollbarChangeMarkers";
		constexpr const char* kHoverInfoKey = "hoverInfo";
		constexpr const char* kHoverPreviewKey = "hoverPreview";
		constexpr const char* kHoverDelayKey = "hoverDelayMs";
		constexpr const char* kColorSchemeKey = "colorScheme";
		constexpr const char* kLanguageServerBinaryKey = "languageServerBinary";
		constexpr const char* kDefaultShellKey = "defaultShell";
		constexpr const char* kClaudeBinaryKey = "claude.binary";
		constexpr std::string_view kClaudePricePrefix = "claude.price.";

		constexpr const char* kSearchRespectGitIgnoreKey = "searchRespectGitIgnore";
		constexpr const char* kSearchProjectsOnlyKey = "searchProjectsOnly";

		constexpr const char* kBackgroundAnalysisKey = "backgroundAnalysis";
		constexpr const char* kAnalysisBudgetKey = "analysisBudgetMegabytes";
		constexpr const char* kAnalysisConcurrencyKey = "analysisConcurrency";

		constexpr std::string_view kKeyBindingPrefix = "key.";

		constexpr const char* kLanguagesKey = "languages";
		constexpr const char* kLanguageServerKey = "language_server";
		constexpr const char* kServersKey = "lsp";
		constexpr const char* kBinaryKey = "binary";
		constexpr const char* kBinaryPathKey = "path";
		constexpr const char* kBinaryArgumentsKey = "arguments";
		constexpr const char* kInitializationOptionsKey = "initialization_options";
		constexpr const char* kServerSettingsKey = "settings";

		constexpr bool kOnWhenAbsent = true;
		constexpr bool kOffWhenAbsent = false;

		constexpr int kCompactIndent = -1;
		constexpr char kIndentCharacter = ' ';
		constexpr bool kEscapeNonAscii = false;

		const nlohmann::json& MemberOrNull(const nlohmann::json& value, std::string_view key)
		{
			static const nlohmann::json absent;

			if (!value.is_object())
			{
				return absent;
			}

			const nlohmann::json::const_iterator member = value.find(std::string{key});

			return member == value.end() ? absent : *member;
		}

		std::string StringOrEmpty(const nlohmann::json& value)
		{
			return value.is_string() ? value.get<std::string>() : std::string{};
		}

		std::vector<std::string> StringsIn(const nlohmann::json& value)
		{
			std::vector<std::string> strings;

			if (!value.is_array())
			{
				return strings;
			}

			for (const nlohmann::json& element : value)
			{
				if (element.is_string())
				{
					strings.push_back(element.get<std::string>());
				}
			}

			return strings;
		}

		std::string ObjectTextOrEmpty(const nlohmann::json& value)
		{
			return value.is_object() ? value.dump() : std::string{};
		}

		constexpr std::string_view kServerOfOldPath = "clangd";
		constexpr std::string_view kLanguageOfOldPath = "Cpp";

		std::vector<std::string> BinaryPathOf(std::string_view serverId)
		{
			return {kServersKey, std::string{serverId}, kBinaryKey, kBinaryPathKey};
		}

		std::vector<std::string> BinaryArgumentsOf(std::string_view serverId)
		{
			return {kServersKey, std::string{serverId}, kBinaryKey, kBinaryArgumentsKey};
		}

		std::string AsJsonText(const nlohmann::json& value)
		{
			return value.dump(kCompactIndent, kIndentCharacter, kEscapeNonAscii, nlohmann::json::error_handler_t::replace);
		}

		bool IsStrictlyUnder(const std::vector<std::string>& path, const std::vector<std::string>& prefix)
		{
			if (path.size() <= prefix.size())
			{
				return false;
			}

			for (std::size_t keyIndex = 0; keyIndex < prefix.size(); ++keyIndex)
			{
				if (path[keyIndex] != prefix[keyIndex])
				{
					return false;
				}
			}

			return true;
		}

		template<typename Number>
			requires std::integral<Number> || std::floating_point<Number>
		Number NumberOrZero(std::string_view stored)
		{
			return Core::ParseNumber<Number>(stored).value_or(Number{});
		}
	}

	AppSettings::AppSettings(std::filesystem::path file)
		: m_File(std::move(file)), m_Document(Core::JsonFile::Load(m_File))
	{
		if (m_Document.IsMalformed())
		{
			APP_WARN("'{}' does not parse; using the default settings", Core::ToUtf8(m_File));
		}

		m_Written = TakeSnapshot(m_Document);
		MoveOldServerPath();
	}

	void AppSettings::MoveOldServerPath()
	{
		const std::vector<std::string> oldKey{kLanguageServerBinaryKey};

		if (!m_Document.GetJsonAt(oldKey).has_value())
		{
			return;
		}

		const std::string oldPath = Core::Trimmed(Get(kLanguageServerBinaryKey));
		m_Document.Remove(kLanguageServerBinaryKey);
		m_Unwritten = true;

		if (oldPath.empty())
		{
			APP_INFO("Dropped '{}': it was blank", kLanguageServerBinaryKey);
			return;
		}

		const bool pathStored = !GetLanguageServerSettings(kServerOfOldPath).BinaryPath.empty();

		if (!pathStored)
		{
			m_Document.SetJsonAt(BinaryPathOf(kServerOfOldPath), AsJsonText(nlohmann::json(oldPath)));
		}

		const bool choiceStored = GetLanguageServerChoice(kLanguageOfOldPath).has_value();

		if (!choiceStored)
		{
			SetLanguageServerChoice(kLanguageOfOldPath, kServerOfOldPath);
		}

		APP_INFO("'{}': {}; {} for {}",
				 kLanguageServerBinaryKey,
				 pathStored ? "dropped because a path was already stored" : "moved to the server's own path",
				 choiceStored ? "the existing choice is kept" : "chose it",
				 kLanguageOfOldPath);
	}

	void AppSettings::Flush()
	{
		if (!m_Unwritten)
		{
			return;
		}

		Core::JsonFile onDisk = Core::JsonFile::Load(m_File);

		if (onDisk.IsMalformed() && !BackUpBeforeWritingOver(m_File, "does not parse"))
		{
			if (!m_WriteFailed)
			{
				APP_WARN("Not writing '{}': it does not parse and could not be copied aside", Core::ToUtf8(m_File));
			}

			m_WriteFailed = true;
			return;
		}

		const bool unreadable = onDisk.GetCount() == 0;

		if (!unreadable)
		{
			const Snapshot currentSnapshot = TakeSnapshot(m_Document);

			for (const auto& [leafPath, leafJson] : m_Written)
			{
				if (!currentSnapshot.contains(leafPath) && !HasLeafUnder(currentSnapshot, leafPath))
				{
					onDisk.RemoveAt(leafPath);
				}
			}

			for (const auto& [leafPath, leafJson] : currentSnapshot)
			{
				const bool unchanged = m_Written.contains(leafPath) && m_Written.at(leafPath) == leafJson;

				if (!unchanged)
				{
					onDisk.SetJsonAt(leafPath, leafJson);
				}
			}
		}

		Core::JsonFile& toWrite = unreadable ? m_Document : onDisk;

		if (!StampSettingsVersion(toWrite))
		{
			m_Unwritten = false;
			APP_WARN("Not writing '{}': a newer version of Graphite wrote it", Core::ToUtf8(m_File));
			return;
		}

		if (const Core::Result<void> saveResult = toWrite.Save(m_File); !saveResult)
		{
			if (!m_WriteFailed)
			{
				APP_WARN("Cannot write '{}': {}", Core::ToUtf8(m_File), saveResult.GetError().Message);
			}

			m_WriteFailed = true;
			return;
		}

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

		for (Core::JsonLeaf& leaf : document.CollectLeaves())
		{
			snapshot.emplace(std::move(leaf.Path), std::move(leaf.Json));
		}

		return snapshot;
	}

	// WARNING: a leaf that became an object keeps its path in the old snapshot, and removing that path would wipe what was just set beneath it.
	bool AppSettings::HasLeafUnder(const Snapshot& snapshot, const std::vector<std::string>& prefix)
	{
		for (const auto& [leafPath, leafJson] : snapshot)
		{
			if (IsStrictlyUnder(leafPath, prefix))
			{
				return true;
			}
		}

		return false;
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

	bool AppSettings::IsExplorerFollowingActiveFile() const
	{
		return ReadFlag(Get(kExplorerFollowsActiveFileKey), kOnWhenAbsent);
	}

	void AppSettings::SetExplorerFollowsActiveFile(bool follow)
	{
		Set(kExplorerFollowsActiveFileKey, Flag(follow));
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

	bool AppSettings::IsIndentingWithSpaces() const
	{
		return ReadFlag(Get(kIndentWithSpacesKey), kOnWhenAbsent);
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

	bool AppSettings::IsWordWrapEnabled() const
	{
		return ReadFlag(Get(kWordWrapKey), kOffWhenAbsent);
	}

	void AppSettings::SetWordWrap(bool wrap)
	{
		Set(kWordWrapKey, Flag(wrap));
	}

	bool AppSettings::IsDiffWordWrapEnabled() const
	{
		return ReadFlag(Get(kDiffWordWrapKey), kOffWhenAbsent);
	}

	void AppSettings::SetDiffWordWrap(bool wrap)
	{
		Set(kDiffWordWrapKey, Flag(wrap));
	}

	std::string AppSettings::GetDiffLayout() const
	{
		return std::string{Get(kDiffLayoutKey)};
	}

	void AppSettings::SetDiffLayout(std::string_view layout)
	{
		Set(kDiffLayoutKey, std::string{layout});
	}

	std::string AppSettings::GetImageBackground() const
	{
		return std::string{Get(kImageBackgroundKey)};
	}

	void AppSettings::SetImageBackground(std::string_view background)
	{
		Set(kImageBackgroundKey, std::string{background});
	}

	std::string AppSettings::GetDiffScope() const
	{
		return std::string{Get(kDiffScopeKey)};
	}

	void AppSettings::SetDiffScope(std::string_view scope)
	{
		Set(kDiffScopeKey, std::string{scope});
	}

	bool AppSettings::IsDiffSyntaxColoured() const
	{
		return ReadFlag(Get(kDiffColourSyntaxKey), kOnWhenAbsent);
	}

	void AppSettings::SetDiffSyntaxColoured(bool colour)
	{
		Set(kDiffColourSyntaxKey, Flag(colour));
	}

	bool AppSettings::HasGutterChangeMarkers() const
	{
		return ReadFlag(Get(kGutterChangeMarkersKey), kOnWhenAbsent);
	}

	void AppSettings::SetGutterChangeMarkers(bool show)
	{
		Set(kGutterChangeMarkersKey, Flag(show));
	}

	bool AppSettings::HasScrollbarChangeMarkers() const
	{
		return ReadFlag(Get(kScrollbarChangeMarkersKey), kOnWhenAbsent);
	}

	void AppSettings::SetScrollbarChangeMarkers(bool show)
	{
		Set(kScrollbarChangeMarkersKey, Flag(show));
	}

	bool AppSettings::IsHoverInfoEnabled() const
	{
		return ReadFlag(Get(kHoverInfoKey), kOnWhenAbsent);
	}

	void AppSettings::SetHoverInfo(bool show)
	{
		Set(kHoverInfoKey, Flag(show));
	}

	std::string AppSettings::GetHoverPreview() const
	{
		return std::string{Get(kHoverPreviewKey)};
	}

	void AppSettings::SetHoverPreview(std::string_view mode)
	{
		Set(kHoverPreviewKey, std::string{mode});
	}

	int AppSettings::GetHoverDelayMs() const
	{
		return NumberOrZero<int>(Get(kHoverDelayKey));
	}

	void AppSettings::SetHoverDelayMs(int milliseconds)
	{
		Set(kHoverDelayKey, std::to_string(milliseconds));
	}

	bool AppSettings::IsSearchRespectingGitIgnore() const
	{
		return ReadFlag(Get(kSearchRespectGitIgnoreKey), kOnWhenAbsent);
	}

	void AppSettings::SetSearchRespectGitIgnore(bool respect)
	{
		Set(kSearchRespectGitIgnoreKey, Flag(respect));
	}

	bool AppSettings::IsSearchProjectsOnly() const
	{
		return ReadFlag(Get(kSearchProjectsOnlyKey), kOffWhenAbsent);
	}

	void AppSettings::SetSearchProjectsOnly(bool projectsOnly)
	{
		Set(kSearchProjectsOnlyKey, Flag(projectsOnly));
	}

	bool AppSettings::IsBackgroundAnalysisEnabled() const
	{
		return ReadFlag(Get(kBackgroundAnalysisKey), kOnWhenAbsent);
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

	std::optional<std::string> AppSettings::GetLanguageServerChoice(std::string_view languageName) const
	{
		const std::vector<std::string> path{kLanguagesKey, std::string{languageName}, kLanguageServerKey};
		const std::string choice = StringOrEmpty(ParseJsonText(m_Document.GetJsonAt(path).value_or(std::string{})));

		if (choice.empty())
		{
			return std::nullopt;
		}

		return choice;
	}

	void AppSettings::SetLanguageServerChoice(std::string_view languageName, std::string_view serverId)
	{
		const std::vector<std::string> path{kLanguagesKey, std::string{languageName}, kLanguageServerKey};

		m_Document.SetJsonAt(path, AsJsonText(nlohmann::json(std::string{serverId})));
		m_Unwritten = true;
	}

	LanguageServerSettings AppSettings::GetLanguageServerSettings(std::string_view serverId) const
	{
		const std::vector<std::string> path{kServersKey, std::string{serverId}};
		const nlohmann::json entry = ParseJsonText(m_Document.GetJsonAt(path).value_or(std::string{}));
		const nlohmann::json& binary = MemberOrNull(entry, kBinaryKey);

		LanguageServerSettings serverSettings{
			.BinaryPath = Core::FromUtf8(StringOrEmpty(MemberOrNull(binary, kBinaryPathKey))),
			.Arguments = StringsIn(MemberOrNull(binary, kBinaryArgumentsKey)),
			.InitializationOptionsJson = ObjectTextOrEmpty(MemberOrNull(entry, kInitializationOptionsKey)),
			.SettingsJson = ObjectTextOrEmpty(MemberOrNull(entry, kServerSettingsKey))};

		return serverSettings;
	}

	void AppSettings::SetLanguageServerBinaryPath(std::string_view serverId, const std::filesystem::path& path)
	{
		const std::vector<std::string> keyPath = BinaryPathOf(serverId);

		if (path.empty())
		{
			m_Document.RemoveAt(keyPath);
		}
		else
		{
			m_Document.SetJsonAt(keyPath, AsJsonText(nlohmann::json(Core::ToUtf8(path))));
		}

		m_Unwritten = true;
	}

	void AppSettings::SetLanguageServerArguments(std::string_view serverId, const std::vector<std::string>& arguments)
	{
		const std::vector<std::string> keyPath = BinaryArgumentsOf(serverId);

		if (arguments.empty())
		{
			m_Document.RemoveAt(keyPath);
		}
		else
		{
			m_Document.SetJsonAt(keyPath, AsJsonText(nlohmann::json(arguments)));
		}

		m_Unwritten = true;
	}

	std::filesystem::path AppSettings::GetClaudeBinary() const
	{
		return Core::FromUtf8(Get(kClaudeBinaryKey));
	}

	void AppSettings::SetClaudeBinary(const std::filesystem::path& binary)
	{
		Set(kClaudeBinaryKey, Core::ToUtf8(binary));
	}

	std::string AppSettings::GetDefaultShell() const
	{
		return Get(kDefaultShellKey);
	}

	void AppSettings::SetDefaultShell(std::string_view profileName)
	{
		Set(kDefaultShellKey, std::string{profileName});
	}

	std::vector<std::pair<std::string, std::string>> AppSettings::GetClaudePrices() const
	{
		std::vector<std::pair<std::string, std::string>> prices;

		for (const std::string& key : m_Document.CollectKeys())
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
		for (const std::string& key : m_Document.CollectKeys())
		{
			if (!std::string_view{key}.starts_with(kKeyBindingPrefix))
			{
				continue;
			}

			const std::string commandId = key.substr(kKeyBindingPrefix.size());
			const std::optional<Core::Binding> binding = Core::ParseBinding(m_Document.Get(key));

			if (!binding.has_value())
			{
				APP_WARN("Ignoring the unreadable key binding stored for '{}'", commandId);
				continue;
			}

			keys.Override(commandId, *binding);
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
