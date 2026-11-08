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
#include "UI/Dialogs/FileDialogOffer.h"

#include "Core/Base/Log.h"
#include "Core/FileSystem/FileSystem.h"
#include "Core/ImGui/TextButton.h"
#include "Core/ImGui/TextInput.h"
#include "Core/Platform/FileDialog.h"
#include "Core/Platform/Shell.h"

#include <IconsLucide.h>

#include <imgui.h>

#include <cstddef>
#include <format>
#include <optional>
#include <utility>

namespace Graphite {

	namespace {

		const std::vector<std::string> kRuntimeButtons{"Get .NET", "Check again"};
		constexpr std::size_t kGetDotnetButton = 0;
		constexpr std::size_t kCheckAgainButton = 1;
		constexpr const char* kBrowseLabel = "Browse";
		constexpr const char* kNoHint = "";
		constexpr float kPaddingSides = 2.0f;

		using ActionLabeller = const char* (*)(InstallAction action);

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
				return "Choose a server to enable go to definition, references and diagnostics.";
			case ServerChoiceKind::Off:
				return "Files are coloured, but there is no go to definition, references or diagnostics.";
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

		std::vector<std::string> ActionLabels(const std::vector<InstallAction>& actions, ActionLabeller labelOf)
		{
			std::vector<std::string> labels;

			for (const InstallAction action : actions)
			{
				labels.emplace_back(labelOf(action));
			}

			return labels;
		}

		std::size_t PositionOf(const std::vector<const ServerDefinition*>& servers, const ServerDefinition* wanted)
		{
			for (std::size_t serverIndex = 0; serverIndex < servers.size(); ++serverIndex)
			{
				if (servers[serverIndex] == wanted)
				{
					return serverIndex;
				}
			}

			return servers.size();
		}

		std::optional<FileKind> LanguageBadge(LanguageId language)
		{
			switch (language)
			{
			case LanguageId::Cpp:
				return FileKind::CppSource;
			case LanguageId::CSharp:
				return FileKind::CSharp;
			case LanguageId::PlainText:
			case LanguageId::TypeScript:
				return std::nullopt;
			}

			return std::nullopt;
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
		return ICON_LC_BRACES;
	}

	std::optional<FileKind> LanguageServerPage::GetBadge() const
	{
		return LanguageBadge(m_Language);
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
		const ServerDefinition* leftCustomServer = CustomServer();
		m_PageCustomServer = nullptr;

		// WARNING: The path is cleared before the choice is stored, so the server is not started once from a path that is about to go.
		ForgetCustomPath(serverId);
		StoreChoice(serverId);

		if (leftCustomServer != nullptr)
		{
			ForgetCustomPath(leftCustomServer->Id);
		}
	}

	void LanguageServerPage::SelectCustom()
	{
		if (CustomServer() != nullptr)
		{
			return;
		}

		const ServerChoice choice = CurrentChoice();
		const bool serverChosen = choice.Kind == ServerChoiceKind::Chosen;

		m_PageCustomServer = serverChosen ? choice.Server : RecommendedServerFor(m_Language);
	}

	void LanguageServerPage::SetCustomPath(const std::filesystem::path& path)
	{
		const ServerDefinition* customServer = CustomServer();

		if (customServer == nullptr)
		{
			return;
		}

		m_PageCustomServer = customServer;

		if (path.empty())
		{
			ForgetCustomPath(customServer->Id);
			return;
		}

		// WARNING: The path is stored before the choice, so a server that is not chosen yet restarts once, when it is.
		SetServerPath(customServer->Id, path);
		StoreChoice(customServer->Id);
	}

	void LanguageServerPage::SetCustomServerType(std::string_view serverId)
	{
		const ServerDefinition* oldServer = CustomServer();
		const ServerDefinition* newServer = FindServer(serverId);
		const bool typeChanges = oldServer != nullptr && ListsServer(newServer) && newServer != oldServer;

		if (!typeChanges)
		{
			return;
		}

		m_PageCustomServer = newServer;

		if (CustomPathInUse(*oldServer).empty())
		{
			return;
		}

		const LanguageServerSettings moved = m_Store.GetLanguageServerSettings(oldServer->Id);
		const std::filesystem::path noPath;
		const std::vector<std::string> noArguments;

		StoreServerBinary(newServer->Id, moved.BinaryPath, moved.Arguments);
		StoreChoice(newServer->Id);
		StoreServerBinary(oldServer->Id, noPath, noArguments);
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

	std::vector<const ServerDefinition*> LanguageServerPage::ListedServers() const
	{
		std::vector<const ServerDefinition*> servers;

		for (const std::string_view serverId : DefinitionOf(m_Language).Servers)
		{
			if (const ServerDefinition* server = FindServer(serverId); server != nullptr)
			{
				servers.push_back(server);
			}
		}

		return servers;
	}

	bool LanguageServerPage::ListsServer(const ServerDefinition* server) const
	{
		for (const ServerDefinition* listed : ListedServers())
		{
			if (listed == server)
			{
				return true;
			}
		}

		return false;
	}

	std::filesystem::path LanguageServerPage::CustomPathInUse(const ServerDefinition& server) const
	{
		const ServerChoice choice = CurrentChoice();
		const bool serverChosen = choice.Kind == ServerChoiceKind::Chosen && choice.Server == &server;

		if (!serverChosen)
		{
			return {};
		}

		return m_Store.GetLanguageServerSettings(server.Id).BinaryPath;
	}

	const ServerDefinition* LanguageServerPage::CustomServer() const
	{
		const ServerChoice choice = CurrentChoice();
		const bool chosenRunsFromCustomPath = choice.Kind == ServerChoiceKind::Chosen && !CustomPathInUse(*choice.Server).empty();

		if (chosenRunsFromCustomPath)
		{
			return choice.Server;
		}

		return m_PageCustomServer;
	}

	// WARNING: A row described before a change made earlier in the same frame is still drawn that frame, so a control
	// whose server is no longer the one shown draws nothing rather than that server's emptied values.
	const ServerDefinition* LanguageServerPage::ShownServer() const
	{
		if (const ServerDefinition* customServer = CustomServer(); customServer != nullptr)
		{
			return customServer;
		}

		const ServerChoice choice = CurrentChoice();
		const bool serverChosen = choice.Kind == ServerChoiceKind::Chosen;

		return serverChosen ? choice.Server : nullptr;
	}

	void LanguageServerPage::StoreChoice(std::string_view serverId)
	{
		const std::string_view settingsName = DefinitionOf(m_Language).SettingsName;

		if (m_Store.GetLanguageServerChoice(settingsName) == serverId)
		{
			return;
		}

		m_Store.SetLanguageServerChoice(settingsName, serverId);
		m_Services.Reconfigure(m_Language);
	}

	void LanguageServerPage::ForgetCustomPath(std::string_view serverId)
	{
		if (m_Store.GetLanguageServerSettings(serverId).BinaryPath.empty())
		{
			return;
		}

		const std::filesystem::path noPath;
		SetServerPath(serverId, noPath);
	}

	void LanguageServerPage::StoreServerBinary(std::string_view serverId, const std::filesystem::path& path, const std::vector<std::string>& arguments)
	{
		m_Store.SetLanguageServerBinaryPath(serverId, path);
		m_Store.SetLanguageServerArguments(serverId, arguments);
		ReconfigureLanguagesRunning(m_Services, serverId);
	}

	std::vector<SettingsCard> LanguageServerPage::Describe()
	{
		const ServerChoice choice = CurrentChoice();
		std::vector<SettingsCard> cards{DescribeServer(choice)};

		if (const ServerDefinition* customServer = CustomServer(); customServer != nullptr)
		{
			cards.push_back(DescribeCustomServer(*customServer));
			return cards;
		}

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

	// WARNING: The server is an entry of the static catalogue, so a row's lambda may keep a reference to it.
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
		const InstallInputs inputs = InstallInputsFor(m_Installs, server);
		const InstallSummary summary = SummariseInstall(inputs);
		const std::optional<ServerCommand> found = m_Services.FoundCommandFor(m_Language);
		const LanguageServerSettings stored = m_Store.GetLanguageServerSettings(server.Id);

		SettingsRow row{.Key = "install",
						.Title = summary.State,
						.Description = summary.Note,
						.Problem = summary.Failure,
						.Keywords = {"install", "update", "uninstall", "version"}};
		std::vector<std::string> labels = ActionLabels(summary.Actions, InstallActionLabel);

		if (IsFoundOnPath(inputs, found, stored.BinaryPath))
		{
			row.Title = FoundOnPathTitle();
			row.Description = JoinSentences(FoundOnPathNote(*found), summary.Note);
			labels = ActionLabels(summary.Actions, FoundOnPathActionLabel);
		}

		if (!summary.Actions.empty())
		{
			row.ControlWidth = SettingsLayout::ButtonsWidth(labels);
			row.DrawControl = [this, serverId = std::string{server.Id}, actions = summary.Actions, labels] { DrawInstallActions(serverId, actions, labels); };
		}

		SettingsCard card{.Heading = "Install"};
		card.Rows.push_back(std::move(row));

		return card;
	}

	SettingsCard LanguageServerPage::DescribeAdvanced(const ServerDefinition& server)
	{
		SettingsCard card{.Heading = "Advanced"};
		card.Rows.push_back(DescribeArguments(server));

		return card;
	}

	SettingsCard LanguageServerPage::DescribeCustomServer(const ServerDefinition& server)
	{
		SettingsCard card{.Heading = "Custom server"};
		const bool languageListsSeveralServers = ListedServers().size() > 1;

		if (languageListsSeveralServers)
		{
			card.Rows.push_back(SettingsRow{.Title = "Server type",
											.Description = "How Graphite talks to the program at the path.",
											.Keywords = {"custom", "kind", "protocol"},
											.ControlWidth = SettingsLayout::ComboWidth(ServerLabels()),
											.DrawControl = [this] { DrawCustomServerType(); }});
		}

		card.Rows.push_back(DescribeCustomPath(server));
		card.Rows.push_back(DescribeArguments(server));

		return card;
	}

	SettingsRow LanguageServerPage::DescribeCustomPath(const ServerDefinition& server)
	{
		const PathRowText text = CustomPathRowText(m_Language, CustomPathInUse(server), m_Services.StatusFor(m_Language));

		return SettingsRow{.Title = "Path",
						   .Description = text.Description,
						   .Problem = text.Problem,
						   .Keywords = {"binary", "executable", "custom"},
						   .WideControl = true,
						   .DrawControl = [this, &server] { DrawCustomPath(server); }};
	}

	SettingsRow LanguageServerPage::DescribeArguments(const ServerDefinition& server)
	{
		return SettingsRow{.Title = "Arguments",
						   .Description = "Passed after the server's own arguments.",
						   .Keywords = {"command line", "flags"},
						   .WideControl = true,
						   .DrawControl = [this, &server] { DrawArguments(server); }};
	}

	std::vector<std::string> LanguageServerPage::ServerLabels() const
	{
		const ServerDefinition* recommended = RecommendedServerFor(m_Language);
		std::vector<std::string> labels;

		for (const ServerDefinition* server : ListedServers())
		{
			labels.push_back(ServerOptionLabel(*server, server == recommended));
		}

		return labels;
	}

	std::vector<std::string> LanguageServerPage::ServerOptionLabels() const
	{
		std::vector<std::string> labels{"None"};

		for (std::string& serverLabel : ServerLabels())
		{
			labels.push_back(std::move(serverLabel));
		}

		labels.emplace_back(CustomChoiceLabel());

		return labels;
	}

	std::vector<std::string> LanguageServerPage::ServerKeywords() const
	{
		std::vector<std::string> keywords;

		for (const ServerDefinition* server : ListedServers())
		{
			keywords.emplace_back(server->Id);
			keywords.emplace_back(server->DisplayName);
		}

		keywords.emplace_back("custom");
		keywords.emplace_back("path");
		keywords.emplace_back("binary");

		return keywords;
	}

	void LanguageServerPage::DrawServerChoice()
	{
		const ServerChoice choice = CurrentChoice();
		const bool readsAsCustom = CustomServer() != nullptr;
		const std::string preview = readsAsCustom ? std::string{CustomChoiceLabel()} : ChoicePreview(m_Language, choice);

		if (!ImGui::BeginCombo("##server", preview.c_str()))
		{
			return;
		}

		const bool turnedOff = !readsAsCustom && choice.Kind == ServerChoiceKind::Off;

		if (ImGui::Selectable("None", turnedOff) && !turnedOff)
		{
			SelectServer(kServerTurnedOff);
		}

		const ServerDefinition* recommended = RecommendedServerFor(m_Language);

		for (const ServerDefinition* server : ListedServers())
		{
			const bool selected = !readsAsCustom && choice.Kind == ServerChoiceKind::Chosen && choice.Server == server;
			const std::string label = ServerOptionLabel(*server, server == recommended);

			if (ImGui::Selectable(label.c_str(), selected) && !selected)
			{
				SelectServer(server->Id);
			}
		}

		if (ImGui::Selectable(CustomChoiceLabel(), readsAsCustom) && !readsAsCustom)
		{
			SelectCustom();
		}

		ImGui::EndCombo();
	}

	void LanguageServerPage::DrawCustomServerType()
	{
		const ServerDefinition* customServer = CustomServer();
		const bool rowIsStale = customServer == nullptr;

		if (rowIsStale)
		{
			return;
		}

		const std::vector<const ServerDefinition*> servers = ListedServers();
		std::size_t selected = PositionOf(servers, customServer);

		if (SettingsLayout::Choice("##serverType", ServerLabels(), selected))
		{
			SetCustomServerType(servers[selected]->Id);
		}
	}

	void LanguageServerPage::DrawRuntimeButtons(const ServerDefinition& server)
	{
		if (Core::TextButton(kRuntimeButtons[kGetDotnetButton].c_str()))
		{
			OpenDotnetDownload(server.Runtime.MinimumMajor);
		}

		ImGui::SameLine();

		if (Core::TextButton(kRuntimeButtons[kCheckAgainButton].c_str()))
		{
			LookForRuntimeAgain();
		}
	}

	void LanguageServerPage::DrawInstallActions(const std::string& serverId, const std::vector<InstallAction>& actions,
												const std::vector<std::string>& labels)
	{
		for (std::size_t actionIndex = 0; actionIndex < actions.size(); ++actionIndex)
		{
			if (actionIndex > 0)
			{
				ImGui::SameLine();
			}

			if (Core::TextButton(labels[actionIndex].c_str()))
			{
				Run(actions[actionIndex], serverId);
			}
		}
	}

	void LanguageServerPage::DrawCustomPath(const ServerDefinition& server)
	{
		const bool rowIsStale = CustomServer() != &server;

		if (rowIsStale)
		{
			return;
		}

		const std::filesystem::path pathInUse = CustomPathInUse(server);

		if (!m_EditingPath || m_PathServer != server.Id)
		{
			m_PathText = Core::ToUtf8(pathInUse);
			m_PathServer = std::string{server.Id};
		}

		const ImGuiStyle& style = ImGui::GetStyle();
		const float browseWidth = ImGui::CalcTextSize(kBrowseLabel).x + style.FramePadding.x * kPaddingSides;

		ImGui::SetNextItemWidth(ImGui::CalcItemWidth() - browseWidth - style.ItemSpacing.x);
		Core::InputTextWithHint("##path", kNoHint, m_PathText);
		m_EditingPath = ImGui::IsItemActive();

		if (ImGui::IsItemDeactivatedAfterEdit())
		{
			const std::filesystem::path edited = Core::FromUtf8(m_PathText);

			if (edited != pathInUse)
			{
				SetCustomPath(edited);
			}
		}

		ImGui::SameLine();
		ImGui::BeginDisabled(m_Browse.IsOpen());

		if (Core::TextButton(kBrowseLabel))
		{
			StartBrowsing(server);
		}

		ImGui::EndDisabled();
		OfferOpenFileDialog(m_Browse);
	}

	void LanguageServerPage::StartBrowsing(const ServerDefinition& server)
	{
		m_Browse.Start(
			ProgramPickerFor(server), [this](const std::filesystem::path& picked) { SetCustomPath(picked); },
			[](const Core::Error& error) { APP_ERROR("Cannot open the program picker: {}", error.Message); });
	}

	void LanguageServerPage::DrawArguments(const ServerDefinition& server)
	{
		const bool rowIsStale = ShownServer() != &server;

		if (rowIsStale)
		{
			return;
		}

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
