#include "Settings/LanguagesSection.h"

#include "Language/Install/ServerInstallActions.h"
#include "Language/Install/ServerInstalls.h"
#include "Language/Registry/LanguageRegistry.h"
#include "Language/Server/DotnetRuntime.h"
#include "Language/Server/LanguageServices.h"
#include "Language/Server/ServerChoice.h"
#include "Settings/AnalysisSettings.h"
#include "Settings/AppSettings.h"
#include "Settings/ArgumentLine.h"
#include "Settings/LanguageTabText.h"
#include "Theme/ActiveScheme.h"
#include "Theme/Palettes.h"

#include "Core/Base/Log.h"
#include "Core/FileSystem/FileSystem.h"
#include "Core/ImGui/TextInput.h"
#include "Core/Platform/FileDialog.h"
#include "Core/Platform/Shell.h"

#include <imgui.h>

#include <algorithm>
#include <format>
#include <utility>
#include <vector>

namespace Graphite {

	namespace {

		void LabelCell(const char* label)
		{
			ImGui::TableNextRow();
			ImGui::TableNextColumn();
			ImGui::AlignTextToFramePadding();
			ImGui::TextUnformatted(label);
			ImGui::TableNextColumn();
		}

		void DimmedWrapped(const std::string& text)
		{
			ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
			ImGui::TextWrapped("%s", text.c_str());
			ImGui::PopStyleColor();
		}

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
	}

	LanguagesSection::LanguagesSection(AnalysisSettings& analysis, AppSettings& store, ServerInstalls& installs,
									   LanguageServices& services, const ActiveScheme& scheme)
		: m_Analysis(analysis), m_Store(store), m_Installs(installs), m_Services(services), m_Scheme(scheme)
	{
	}

	void LanguagesSection::SelectLanguage(LanguageId language)
	{
		m_RequestedLanguage = language;
	}

	void LanguagesSection::OnDeactivated()
	{
		m_EditingPath = false;
		m_EditingArguments = false;
	}

	void LanguagesSection::SelectServer(LanguageId language, std::string_view serverId)
	{
		m_Store.SetLanguageServerChoice(DefinitionOf(language).SettingsName, serverId);
		m_Services.Reconfigure(language);
	}

	void LanguagesSection::SetServerPath(std::string_view serverId, const std::filesystem::path& path)
	{
		m_Store.SetLanguageServerBinaryPath(serverId, path);
		ReconfigureLanguagesRunning(m_Services, serverId);
	}

	void LanguagesSection::SetServerArguments(std::string_view serverId, std::string_view line)
	{
		m_Store.SetLanguageServerArguments(serverId, SplitArgumentLine(line));
		ReconfigureLanguagesRunning(m_Services, serverId);
	}

	void LanguagesSection::Run(InstallAction action, std::string_view serverId)
	{
		// A refusal means a job for this server already runs, which the tab is already showing.
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

	void LanguagesSection::LookForRuntimeAgain(LanguageId language)
	{
		m_Services.Reconfigure(language);
	}

	void LanguagesSection::OnImGuiRender()
	{
		m_Browse.Poll();

		if (ImGui::BeginTabBar("##languages"))
		{
			for (const LanguageId language : LanguagesWithServers())
			{
				const ImGuiTabItemFlags flags = m_RequestedLanguage == language ? ImGuiTabItemFlags_SetSelected : ImGuiTabItemFlags_None;
				const std::string label{LanguageDisplayName(language)};

				if (ImGui::BeginTabItem(label.c_str(), nullptr, flags))
				{
					m_ShownLanguage = language;
					RenderLanguage(language);
					ImGui::EndTabItem();
				}
			}

			ImGui::EndTabBar();
		}

		m_RequestedLanguage.reset();

		RenderBackgroundAnalysis();
	}

	void LanguagesSection::RenderLanguage(LanguageId language)
	{
		const ServerChoice choice = ChooseServer(language, m_Store.GetLanguageServerChoice(DefinitionOf(language).SettingsName));

		if (!ImGui::BeginTable("##languageServer", 2))
		{
			return;
		}

		ImGui::TableSetupColumn("label", ImGuiTableColumnFlags_WidthFixed);
		ImGui::TableSetupColumn("value", ImGuiTableColumnFlags_WidthStretch);

		LabelCell("Language server");
		RenderServerChoice(language, choice);

		switch (choice.Kind)
		{
		case ServerChoiceKind::NotConfigured:
			DimmedWrapped("Choose a server to enable go to definition, completion and diagnostics.");
			break;
		case ServerChoiceKind::Off:
			DimmedWrapped("Files are coloured, but there is no go to definition, completion or diagnostics.");
			break;
		case ServerChoiceKind::Chosen:
		{
			const ServerDefinition& server = *choice.Server;
			const LanguageServerSettings stored = m_Store.GetLanguageServerSettings(server.Id);

			DimmedWrapped(std::string{server.Description});

			if (server.Runtime.Kind != RuntimeKind::None)
			{
				LabelCell("Requires");
				RenderRuntime(language, server);
			}

			LabelCell("State");
			RenderInstall(server);

			LabelCell("Custom path");
			RenderCustomPath(server, stored);

			LabelCell("Arguments");
			RenderArguments(server, stored);
			break;
		}
		}

		ImGui::EndTable();
	}

	void LanguagesSection::RenderServerChoice(LanguageId language, const ServerChoice& choice)
	{
		const LanguageDefinition& definition = DefinitionOf(language);
		const std::string preview = ChoicePreview(language, choice);

		ImGui::SetNextItemWidth(-FLT_MIN);

		if (!ImGui::BeginCombo("##server", preview.c_str()))
		{
			return;
		}

		if (ImGui::Selectable("None", choice.Kind == ServerChoiceKind::Off))
		{
			SelectServer(language, kServerTurnedOff);
		}

		for (std::size_t index = 0; index < definition.Servers.size(); ++index)
		{
			const ServerDefinition* server = FindServer(definition.Servers[index]);

			if (server == nullptr)
			{
				continue;
			}

			const bool selected = choice.Kind == ServerChoiceKind::Chosen && choice.Server == server;
			const std::string label = ServerOptionLabel(*server, index == 0);

			if (ImGui::Selectable(label.c_str(), selected))
			{
				SelectServer(language, server->Id);
			}
		}

		ImGui::EndCombo();
	}

	void LanguagesSection::RenderRuntime(LanguageId language, const ServerDefinition& server)
	{
		const std::vector<DotnetInstallation>& installations = m_Services.DotnetInstallations();

		ImGui::AlignTextToFramePadding();
		ImGui::TextUnformatted(RuntimeLine(server.Runtime, installations).c_str());

		if (RuntimeFound(server.Runtime, installations))
		{
			return;
		}

		ImGui::SameLine();

		if (ImGui::Button("Get .NET"))
		{
			OpenDotnetDownload(server.Runtime.MinimumMajor);
		}

		ImGui::SameLine();

		if (ImGui::Button("Check again"))
		{
			LookForRuntimeAgain(language);
		}
	}

	void LanguagesSection::RenderInstall(const ServerDefinition& server)
	{
		const InstallSummary summary = SummariseInstall(InstallInputsFor(m_Installs, server));

		ImGui::AlignTextToFramePadding();
		ImGui::TextUnformatted(summary.State.c_str());

		for (const InstallAction action : summary.Actions)
		{
			ImGui::SameLine();

			if (ImGui::Button(InstallActionLabel(action)))
			{
				Run(action, server.Id);
			}
		}

		if (!summary.Note.empty())
		{
			DimmedWrapped(summary.Note);
		}

		if (!summary.Failure.empty())
		{
			ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(ReadEditorPalette(m_Scheme.Get()).DiagnosticError));
			ImGui::TextWrapped("%s", summary.Failure.c_str());
			ImGui::PopStyleColor();
		}
	}

	void LanguagesSection::RenderCustomPath(const ServerDefinition& server, const LanguageServerSettings& stored)
	{
		if (!m_EditingPath || m_PathServer != server.Id)
		{
			m_PathText = Core::ToUtf8(stored.BinaryPath);
			m_PathServer = std::string{server.Id};
		}

		const ImGuiStyle& style = ImGui::GetStyle();
		const float browseWidth = ImGui::CalcTextSize("Browse").x + style.FramePadding.x * 2.0f;

		ImGui::SetNextItemWidth(-(browseWidth + style.ItemSpacing.x));
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

		if (ImGui::Button("Browse"))
		{
			StartBrowsing(server);
		}

		ImGui::EndDisabled();

		DimmedWrapped("Empty: Graphite's install, then PATH");
	}

	void LanguagesSection::StartBrowsing(const ServerDefinition& server)
	{
		m_Browse.Start(ProgramPickerFor(server), [this, serverId = std::string{server.Id}](Core::Result<std::filesystem::path> picked) {
			if (picked)
			{
				SetServerPath(serverId, picked.GetValue());
			}
		});
	}

	void LanguagesSection::RenderArguments(const ServerDefinition& server, const LanguageServerSettings& stored)
	{
		if (!m_EditingArguments || m_ArgumentsServer != server.Id)
		{
			m_ArgumentsText = JoinArgumentLine(stored.Arguments);
			m_ArgumentsServer = std::string{server.Id};
		}

		ImGui::SetNextItemWidth(-FLT_MIN);
		Core::InputTextWithHint("##arguments", "after the server's own", m_ArgumentsText);
		m_EditingArguments = ImGui::IsItemActive();

		if (ImGui::IsItemDeactivatedAfterEdit() && SplitArgumentLine(m_ArgumentsText) != stored.Arguments)
		{
			SetServerArguments(server.Id, m_ArgumentsText);
		}
	}

	void LanguagesSection::RenderBackgroundAnalysis()
	{
		ImGui::SeparatorText("Background analysis");

		bool enabled = m_Analysis.IsEnabled();

		if (ImGui::Checkbox("Analyse the workspace in the background", &enabled))
		{
			m_Analysis.SetEnabled(enabled);
		}

		ImGui::TextWrapped("A few files at a time are sent to the language server, ahead of being "
						   "opened, so that a file opens already coloured. The pass waits while "
						   "the file you are reading has questions of its own outstanding.");

		// Disabled rather than hidden, the same reasoning SearchSection gives for its own
		// project-only checkbox: a control that vanishes reads as a bug, and there is nothing to
		// budget once the pass itself is off.
		ImGui::BeginDisabled(!enabled);

		int budget = static_cast<int>(m_Analysis.GetBudgetMegabytes());

		// Set on release, not on every frame of the drag: SetBudgetMegabytes changes AppSettings,
		// which GraphiteLayer writes out at the end of any frame that changed it, and a drag across
		// the range produces a new value every frame. Same shape EditorSection
		// uses for its font-size and tab-width sliders, and the same reason.
		if (ImGui::SliderInt("Analysis kept (MB)", &budget,
							 static_cast<int>(AnalysisSettings::kMinimumBudgetMegabytes),
							 static_cast<int>(AnalysisSettings::kMaximumBudgetMegabytes),
							 "%d", ImGuiSliderFlags_AlwaysClamp))
		{
			m_PendingBudgetMegabytes = budget;
		}

		if (ImGui::IsItemDeactivatedAfterEdit() && m_PendingBudgetMegabytes.has_value())
		{
			m_Analysis.SetBudgetMegabytes(static_cast<std::size_t>(*m_PendingBudgetMegabytes));
			m_PendingBudgetMegabytes.reset();
		}

		bool concurrencyAutomatic = m_Analysis.IsConcurrencyAutomatic();

		if (ImGui::Checkbox("Set the number of files automatically", &concurrencyAutomatic))
		{
			m_Analysis.SetConcurrencyAutomatic(concurrencyAutomatic);
		}

		// Disabled whenever the checkbox above is ticked, on top of the section's own condition:
		// automatic means the number is not this control's to set, not that it stops existing.
		ImGui::BeginDisabled(concurrencyAutomatic);

		int concurrency = static_cast<int>(m_Analysis.GetConcurrency());

		// The label carries the automatic figure while that is what governs, so ticking the box
		// does not also make the number a mystery — a fixed "Files analysed at once" would leave
		// nothing on screen saying what "automatically" actually chose.
		const std::string label =
			concurrencyAutomatic ? std::format("Files analysed at once (automatic: {})###FilesAtOnce", concurrency)
								 : std::string{"Files analysed at once###FilesAtOnce"};

		// An input rather than a slider, whose grab covers a range this short. It reports a change
		// when the field is left or a step button is clicked, not per keystroke, so the settings
		// file is written once. Clamped here because a typed value is not bounded by the widget.
		if (ImGui::InputInt(label.c_str(), &concurrency))
		{
			const int clamped = std::clamp(concurrency, static_cast<int>(AnalysisSettings::kMinimumConcurrency),
										   static_cast<int>(AnalysisSettings::kMaximumConcurrency));
			m_Analysis.SetConcurrency(static_cast<std::size_t>(clamped));
		}

		ImGui::EndDisabled();

		ImGui::EndDisabled();
	}
}
