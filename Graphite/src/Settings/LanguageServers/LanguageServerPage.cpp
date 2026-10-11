#include "Settings/LanguageServers/LanguageServerPage.h"

#include "Language/Install/ServerInstallActions.h"
#include "Language/Install/ServerInstalls.h"
#include "Language/Registry/LanguageRegistry.h"
#include "Language/Server/DotnetRuntime.h"
#include "Language/Server/LanguageServices.h"
#include "Language/Server/ServerCatalog.h"
#include "Language/Server/ServerChoice.h"
#include "Language/Server/ServerCommand.h"
#include "Settings/AppSettings.h"
#include "Settings/ArgumentLine.h"
#include "Settings/LanguageServers/LanguageTabText.h"
#include "Settings/Panel/SettingsLayout.h"

#include "Core/Base/Log.h"
#include "Core/FileSystem/FileSystem.h"
#include "Core/ImGui/TextInput.h"
#include "Core/Platform/FileDialog.h"
#include "Core/Platform/Shell.h"

#include <IconsMaterialDesignIcons.h>

#include <imgui.h>

#include <format>
#include <optional>
#include <utility>

namespace Graphite {

	namespace {

		const std::vector<std::string> kRuntimeButtons{"Get .NET", "Check again"};
		constexpr std::size_t kGetDotnetButton = 0;
		constexpr std::size_t kCheckAgainButton = 1;
		constexpr const char* kBrowseLabel = "Browse";

		Core::AsyncDialog::Picker ProgramPickerFor(const ServerDefinition& server)
		{
			std::string title = std::format("Choose the {} language server", server.DisplayName);

			return [title = std::move(title)] {
				const std::vector<Core::FileDialog::Filter> filters{{"Programs and .NET assemblies", "*.exe;*.dll"}, {"All files", "*.*"}};
				return Core::FileDialog::PickFile(title, filters);
			};
		}

		void OpenDotnetDownload(int major)
		{
			const std::string address = DotnetDownloadUrl(major);

			if (const Core::Result<void> opened = Core::Shell::OpenInBrowser(address); !opened)
			{
				APP_WARN("Cannot open {}: {}", address, opened.GetError().Message);
			}
		}

		std::string ChoiceDescription(const ServerChoice& choice)
		{
			switch (choice.Kind)
			{
			case ServerChoiceKind::NotConfigured:
				return "Choose a server to enable go to definition, completion and diagnostics.";
			case ServerChoiceKind::Off:
				return "Files are coloured, but there is no go to definition, completion or diagnostics.";
			case ServerChoiceKind::Chosen:
				break;
			}

			return std::string{choice.Server->Description};
		}

		std::string JoinSentences(const std::string& first, const std::string& second)
		{
			if (first.empty())
			{
				return second;
			}

			if (second.empty())
			{
				return first;
			}

			return first + " " + second;
		}

		std::vector<std::string> ActionLabels(const std::vector<InstallAction>& actions)
		{
			std::vector<std::string> labels;

			for (const InstallAction action : actions)
			{
				labels.emplace_back(InstallActionLabel(action));
			}

			return labels;
		}

		const char* LanguageIcon(LanguageId language)
		{
			if (language == LanguageId::Cpp)
			{
				return ICON_MDI_LANGUAGE_CPP;
			}

			if (language == LanguageId::CSharp)
			{
				return ICON_MDI_LANGUAGE_CSHARP;
			}

			return ICON_MDI_CODE_BRACES;
		}
	}

	LanguageServerPage::LanguageServerPage(LanguageId language, AppSettings& store, ServerInstalls& installs, LanguageServices& services)
		: m_Language(language), m_Store(store), m_Installs(installs), m_Services(services)
	{
	}

	std::string_view LanguageServerPage::GetId() const
	{
		return DefinitionOf(m_Language).SettingsName;
	}

	std::string_view LanguageServerPage::GetTitle() const
	{
		return LanguageDisplayName(m_Language);
	}

	const char* LanguageServerPage::GetIcon() const
	{
		return LanguageIcon(m_Language);
	}

	void LanguageServerPage::Update()
	{
		m_Browse.Poll();
	}

	void LanguageServerPage::OnDeactivated()
	{
		m_EditingPath = false;
		m_EditingArguments = false;
	}

	void LanguageServerPage::SelectServer(std::string_view serverId)
	{
		const std::string_view settingsName = DefinitionOf(m_Language).SettingsName;

		if (m_Store.GetLanguageServerChoice(settingsName) == serverId)
		{
			return;
		}

		m_Store.SetLanguageServerChoice(settingsName, serverId);
		m_Services.Reconfigure(m_Language);
	}

	void LanguageServerPage::SetServerPath(std::string_view serverId, const std::filesystem::path& path)
	{
		m_Store.SetLanguageServerBinaryPath(serverId, path);
		ReconfigureLanguagesRunning(m_Services, serverId);
	}

	void LanguageServerPage::SetServerArguments(std::string_view serverId, std::string_view line)
	{
		m_Store.SetLanguageServerArguments(serverId, SplitArgumentLine(line));
		ReconfigureLanguagesRunning(m_Services, serverId);
	}

	void LanguageServerPage::Run(InstallAction action, std::string_view serverId)
	{
		// A refusal means a job for this server already runs, which the page is already showing.
		switch (action)
		{
		case InstallAction::Install:
		case InstallAction::Update:
			m_Installs.Install(serverId);
			break;
		case InstallAction::Cancel:
			m_Installs.Cancel(serverId);
			break;
		case InstallAction::CheckForUpdates:
			m_Installs.CheckForUpdates(serverId);
			break;
		case InstallAction::Uninstall:
			UninstallStoppingServers(m_Installs, m_Services, serverId);
			break;
		}
	}

	void LanguageServerPage::LookForRuntimeAgain()
	{
		m_Services.Reconfigure(m_Language);
	}

	ServerChoice LanguageServerPage::CurrentChoice() const
	{
		return ChooseServer(m_Language, m_Store.GetLanguageServerChoice(DefinitionOf(m_Language).SettingsName));
	}

	std::vector<SettingsCard> LanguageServerPage::Describe()
	{
		const ServerChoice choice = CurrentChoice();
		std::vector<SettingsCard> cards{DescribeServer(choice)};

		if (choice.Kind == ServerChoiceKind::Chosen)
		{
			cards.push_back(DescribeInstall(*choice.Server));
			cards.push_back(DescribeAdvanced(*choice.Server));
		}

		return cards;
	}

	SettingsCard LanguageServerPage::DescribeServer(const ServerChoice& choice)
	{
		SettingsCard card{.Heading = "Server"};

		card.Rows.push_back(SettingsRow{.Title = "Language server",
										.Description = ChoiceDescription(choice),
										.Keywords = ServerKeywords(),
										.ControlWidth = SettingsLayout::ComboWidth(ServerOptionLabels()),
										.DrawControl = [this] { DrawServerChoice(); }});

		if (choice.Kind == ServerChoiceKind::Chosen && choice.Server->Runtime.Kind != RuntimeKind::None)
		{
			card.Rows.push_back(DescribeRuntime(*choice.Server));
		}

		return card;
	}

	// The server is an entry of the static catalogue, so a row's lambda may keep a reference to it.
	SettingsRow LanguageServerPage::DescribeRuntime(const ServerDefinition& server)
	{
		const std::vector<DotnetInstallation>& installations = m_Services.DotnetInstallations();
		const std::string line = RuntimeLine(server.Runtime, installations);

		if (RuntimeFound(server.Runtime, installations))
		{
			return SettingsRow{.Title = "Requires", .Description = line};
		}

		return SettingsRow{.Title = "Requires",
						   .Problem = line,
						   .ControlWidth = SettingsLayout::ButtonsWidth(kRuntimeButtons),
						   .DrawControl = [this, &server] { DrawRuntimeButtons(server); }};
	}

	SettingsCard LanguageServerPage::DescribeInstall(const ServerDefinition& server)
	{
		const LanguageServerSettings stored = m_Store.GetLanguageServerSettings(server.Id);
		const InstallSummary summary = SummariseInstall(InstallInputsFor(m_Installs, server));

		SettingsRow row{.Key = "install",
						.Title = summary.State,
						.Description = JoinSentences(summary.Note, RunningElsewhere(stored)),
						.Problem = summary.Failure,
						.Keywords = {"install", "update", "uninstall", "version"}};

		if (!summary.Actions.empty())
		{
			row.ControlWidth = SettingsLayout::ButtonsWidth(ActionLabels(summary.Actions));
			row.DrawControl = [this, serverId = std::string{server.Id}, actions = summary.Actions] { DrawInstallActions(serverId, actions); };
		}

		SettingsCard card{.Heading = "Install"};
		card.Rows.push_back(std::move(row));

		return card;
	}

	SettingsCard LanguageServerPage::DescribeAdvanced(const ServerDefinition& server)
	{
		SettingsCard card{.Heading = "Advanced"};

		card.Rows.push_back(SettingsRow{.Title = "Custom path",
										.Description = "Empty: Graphite's install, then PATH.",
										.Keywords = {"binary", "executable"},
										.WideControl = true,
										.DrawControl = [this, &server] { DrawCustomPath(server); }});
		card.Rows.push_back(SettingsRow{.Title = "Arguments",
										.Description = "Passed after the server's own arguments.",
										.Keywords = {"command line", "flags"},
										.WideControl = true,
										.DrawControl = [this, &server] { DrawArguments(server); }});

		return card;
	}

	std::vector<std::string> LanguageServerPage::ServerOptionLabels() const
	{
		const LanguageDefinition& definition = DefinitionOf(m_Language);
		std::vector<std::string> labels{"None"};

		for (std::size_t serverIndex = 0; serverIndex < definition.Servers.size(); ++serverIndex)
		{
			if (const ServerDefinition* server = FindServer(definition.Servers[serverIndex]); server != nullptr)
			{
				const bool recommended = serverIndex == 0;
				labels.push_back(ServerOptionLabel(*server, recommended));
			}
		}

		return labels;
	}

	std::vector<std::string> LanguageServerPage::ServerKeywords() const
	{
		std::vector<std::string> keywords;

		for (const std::string_view serverId : DefinitionOf(m_Language).Servers)
		{
			if (const ServerDefinition* server = FindServer(serverId); server != nullptr)
			{
				keywords.emplace_back(server->Id);
				keywords.emplace_back(server->DisplayName);
			}
		}

		return keywords;
	}

	std::string LanguageServerPage::RunningElsewhere(const LanguageServerSettings& stored) const
	{
		const std::optional<ServerCommand> found = m_Services.FoundCommandFor(m_Language);

		if (!found.has_value())
		{
			return {};
		}

		const bool customPathSet = !stored.BinaryPath.empty();

		return RunningElsewhereNote(*found, customPathSet);
	}

	void LanguageServerPage::DrawServerChoice()
	{
		const ServerChoice choice = CurrentChoice();
		const LanguageDefinition& definition = DefinitionOf(m_Language);
		const std::string preview = ChoicePreview(m_Language, choice);

		if (!ImGui::BeginCombo("##server", preview.c_str()))
		{
			return;
		}

		const bool turnedOff = choice.Kind == ServerChoiceKind::Off;

		if (ImGui::Selectable("None", turnedOff) && !turnedOff)
		{
			SelectServer(kServerTurnedOff);
		}

		for (std::size_t serverIndex = 0; serverIndex < definition.Servers.size(); ++serverIndex)
		{
			const ServerDefinition* server = FindServer(definition.Servers[serverIndex]);

			if (server == nullptr)
			{
				continue;
			}

			const bool selected = choice.Kind == ServerChoiceKind::Chosen && choice.Server == server;
			const bool recommended = serverIndex == 0;
			const std::string label = ServerOptionLabel(*server, recommended);

			if (ImGui::Selectable(label.c_str(), selected) && !selected)
			{
				SelectServer(server->Id);
			}
		}

		ImGui::EndCombo();
	}

	void LanguageServerPage::DrawRuntimeButtons(const ServerDefinition& server)
	{
		if (ImGui::Button(kRuntimeButtons[kGetDotnetButton].c_str()))
		{
			OpenDotnetDownload(server.Runtime.MinimumMajor);
		}

		ImGui::SameLine();

		if (ImGui::Button(kRuntimeButtons[kCheckAgainButton].c_str()))
		{
			LookForRuntimeAgain();
		}
	}

	void LanguageServerPage::DrawInstallActions(const std::string& serverId, const std::vector<InstallAction>& actions)
	{
		bool firstAction = true;

		for (const InstallAction action : actions)
		{
			if (!firstAction)
			{
				ImGui::SameLine();
			}

			firstAction = false;

			if (ImGui::Button(InstallActionLabel(action)))
			{
				Run(action, serverId);
			}
		}
	}

	void LanguageServerPage::DrawCustomPath(const ServerDefinition& server)
	{
		const LanguageServerSettings stored = m_Store.GetLanguageServerSettings(server.Id);

		if (!m_EditingPath || m_PathServer != server.Id)
		{
			m_PathText = Core::ToUtf8(stored.BinaryPath);
			m_PathServer = std::string{server.Id};
		}

		const ImGuiStyle& style = ImGui::GetStyle();
		const float browseWidth = ImGui::CalcTextSize(kBrowseLabel).x + style.FramePadding.x * 2.0f;

		ImGui::SetNextItemWidth(ImGui::CalcItemWidth() - browseWidth - style.ItemSpacing.x);
		Core::InputTextWithHint("##path", "", m_PathText);
		m_EditingPath = ImGui::IsItemActive();

		if (ImGui::IsItemDeactivatedAfterEdit())
		{
			const std::filesystem::path edited = Core::FromUtf8(m_PathText);

			if (edited != stored.BinaryPath)
			{
				SetServerPath(server.Id, edited);
			}
		}

		ImGui::SameLine();
		ImGui::BeginDisabled(m_Browse.IsOpen());

		if (ImGui::Button(kBrowseLabel))
		{
			StartBrowsing(server);
		}

		ImGui::EndDisabled();
	}

	void LanguageServerPage::StartBrowsing(const ServerDefinition& server)
	{
		m_Browse.Start(ProgramPickerFor(server), [this, serverId = std::string{server.Id}](Core::Result<std::filesystem::path> picked) {
			if (picked)
			{
				SetServerPath(serverId, picked.GetValue());
			}
		});
	}

	void LanguageServerPage::DrawArguments(const ServerDefinition& server)
	{
		const LanguageServerSettings stored = m_Store.GetLanguageServerSettings(server.Id);

		if (!m_EditingArguments || m_ArgumentsServer != server.Id)
		{
			m_ArgumentsText = JoinArgumentLine(stored.Arguments);
			m_ArgumentsServer = std::string{server.Id};
		}

		Core::InputTextWithHint("##arguments", "after the server's own", m_ArgumentsText);
		m_EditingArguments = ImGui::IsItemActive();

		if (ImGui::IsItemDeactivatedAfterEdit() && SplitArgumentLine(m_ArgumentsText) != stored.Arguments)
		{
			SetServerArguments(server.Id, m_ArgumentsText);
		}
	}
}
