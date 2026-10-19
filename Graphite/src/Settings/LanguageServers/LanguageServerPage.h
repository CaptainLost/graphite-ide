#pragma once

#include "Language/Api/LanguageId.h"
#include "Language/Install/InstallSummary.h"
#include "Settings/Panel/SettingsPage.h"

#include "Core/Platform/AsyncDialog.h"

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace Graphite {

	class AppSettings;
	class LanguageServices;
	class ServerInstalls;
	struct ServerChoice;
	struct ServerDefinition;

	class LanguageServerPage final : public SettingsPage
	{
	public:
		LanguageServerPage(LanguageId language, AppSettings& store, ServerInstalls& installs, LanguageServices& services);

		std::string_view GetId() const override;

		std::string_view GetTitle() const override;

		const char* GetIcon() const override;

		std::optional<FileKind> GetBadge() const override;

		std::vector<SettingsCard> Describe() override;

		void Update() override;

		void OnDeactivated() override;

		void SelectServer(std::string_view serverId);
		void SelectCustom();
		void SetCustomPath(const std::filesystem::path& path);
		void SetCustomServerType(std::string_view serverId);
		void SetServerPath(std::string_view serverId, const std::filesystem::path& path);
		void SetServerArguments(std::string_view serverId, std::string_view line);
		void Run(InstallAction action, std::string_view serverId);
		void LookForRuntimeAgain();

	private:
		ServerChoice CurrentChoice() const;
		std::vector<const ServerDefinition*> ListedServers() const;
		bool ListsServer(const ServerDefinition* server) const;
		std::filesystem::path CustomPathInUse(const ServerDefinition& server) const;
		const ServerDefinition* CustomServer() const;
		const ServerDefinition* ShownServer() const;

		void StoreChoice(std::string_view serverId);
		void ForgetCustomPath(std::string_view serverId);
		void StoreServerBinary(std::string_view serverId, const std::filesystem::path& path, const std::vector<std::string>& arguments);

		SettingsCard DescribeServer(const ServerChoice& choice);
		SettingsRow DescribeRuntime(const ServerDefinition& server);
		SettingsCard DescribeInstall(const ServerDefinition& server);
		SettingsCard DescribeAdvanced(const ServerDefinition& server);
		SettingsCard DescribeCustomServer(const ServerDefinition& server);
		SettingsRow DescribeCustomPath(const ServerDefinition& server);
		SettingsRow DescribeArguments(const ServerDefinition& server);

		std::vector<std::string> ServerLabels() const;
		std::vector<std::string> ServerOptionLabels() const;
		std::vector<std::string> ServerKeywords() const;

		void DrawServerChoice();
		void DrawCustomServerType();
		void DrawRuntimeButtons(const ServerDefinition& server);
		void DrawInstallActions(const std::string& serverId, const std::vector<InstallAction>& actions, const std::vector<std::string>& labels);
		void DrawCustomPath(const ServerDefinition& server);
		void DrawArguments(const ServerDefinition& server);
		void StartBrowsing(const ServerDefinition& server);

		LanguageId m_Language;
		AppSettings& m_Store;
		ServerInstalls& m_Installs;
		LanguageServices& m_Services;

		const ServerDefinition* m_PageCustomServer = nullptr;

		// WARNING: A field's text is kept while it is edited, with whose it is, so the stored value does not overwrite what
		// is being typed and a server switched mid-edit does not show another server's text.
		std::string m_PathText;
		std::string m_PathServer;
		bool m_EditingPath = false;
		std::string m_ArgumentsText;
		std::string m_ArgumentsServer;
		bool m_EditingArguments = false;

		Core::AsyncDialog m_Browse;
	};
}
