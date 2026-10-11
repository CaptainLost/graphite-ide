#pragma once

#include "Language/Api/LanguageId.h"
#include "Language/Install/InstallSummary.h"
#include "Settings/SettingsSection.h"

#include "Core/Platform/AsyncDialog.h"

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace Graphite {

	class ActiveScheme;
	class AnalysisSettings;
	class AppSettings;
	class LanguageServices;
	class ServerInstalls;
	struct LanguageServerSettings;
	struct ServerChoice;
	struct ServerDefinition;

	class LanguagesSection final : public SettingsSection
	{
	public:
		LanguagesSection(AnalysisSettings& analysis, AppSettings& store, ServerInstalls& installs, LanguageServices& services,
						 const ActiveScheme& scheme);

		std::string_view GetTitle() const override { return "Languages"; }

		void OnImGuiRender() override;

		void OnDeactivated() override;

		// Takes effect the next time the section is drawn.
		void SelectLanguage(LanguageId language);

		std::optional<LanguageId> GetShownLanguage() const { return m_ShownLanguage; }

		void SelectServer(LanguageId language, std::string_view serverId);
		void SetServerPath(std::string_view serverId, const std::filesystem::path& path);
		void SetServerArguments(std::string_view serverId, std::string_view line);
		void Run(InstallAction action, std::string_view serverId);
		void LookForRuntimeAgain(LanguageId language);

	private:
		void RenderLanguage(LanguageId language);
		void RenderServerChoice(LanguageId language, const ServerChoice& choice);
		void RenderRuntime(LanguageId language, const ServerDefinition& server);
		void RenderInstall(const ServerDefinition& server);
		void RenderCustomPath(const ServerDefinition& server, const LanguageServerSettings& stored);
		void RenderArguments(const ServerDefinition& server, const LanguageServerSettings& stored);
		void RenderBackgroundAnalysis();
		void StartBrowsing(const ServerDefinition& server);

		AnalysisSettings& m_Analysis;
		AppSettings& m_Store;
		ServerInstalls& m_Installs;
		LanguageServices& m_Services;
		const ActiveScheme& m_Scheme;

		std::optional<LanguageId> m_RequestedLanguage;
		std::optional<LanguageId> m_ShownLanguage;

		// A field's text while it is edited, and whose it is, so the stored value does not overwrite what is being
		// typed, and a tab switched mid-edit does not show another server's text.
		std::string m_PathText;
		std::string m_PathServer;
		bool m_EditingPath = false;
		std::string m_ArgumentsText;
		std::string m_ArgumentsServer;
		bool m_EditingArguments = false;

		Core::AsyncDialog m_Browse;

		// The slider's value while it is dragged, committed on release so a drag writes graphite.json once.
		std::optional<int> m_PendingBudgetMegabytes;
	};
}
