#include "UI/LanguageSupportBar.h"

#include "Language/Registry/LanguageRegistry.h"

#include <IconsMaterialDesignIcons.h>

#include <imgui.h>

#include <algorithm>
#include <format>

namespace Graphite {

	namespace {

		bool Offers(const InstallSummary& summary, InstallAction action)
		{
			return std::find(summary.Actions.begin(), summary.Actions.end(), action) != summary.Actions.end();
		}

		LanguagePrompt InstallingPrompt(LanguageId language, const ServerDefinition& server, const InstallSummary& summary)
		{
			LanguagePrompt prompt{.Language = language,
								  .ServerId = std::string{server.Id},
								  .Message = std::format("Installing {} for {}: {}", server.DisplayName, LanguageDisplayName(language), summary.State)};

			if (Offers(summary, InstallAction::Cancel))
			{
				prompt.Buttons.push_back(LanguageBarChoice::Cancel);
			}

			prompt.Buttons.push_back(LanguageBarChoice::NotNow);
			return prompt;
		}

		LanguagePrompt NotInstalledPrompt(LanguageId language, const ServerDefinition& server, const InstallSummary& summary)
		{
			LanguagePrompt prompt{.Language = language,
								  .ServerId = std::string{server.Id},
								  .Message = std::format("{} support needs the {} language server, which is not installed.",
														 LanguageDisplayName(language), server.DisplayName)};

			if (Offers(summary, InstallAction::Install))
			{
				prompt.Buttons.push_back(LanguageBarChoice::Install);
			}

			prompt.Buttons.push_back(LanguageBarChoice::OpenSettings);
			prompt.Buttons.push_back(LanguageBarChoice::NotNow);
			return prompt;
		}

		LanguagePrompt MissingRuntimePrompt(LanguageId language, const ServerDefinition& server)
		{
			return LanguagePrompt{.Language = language,
								  .ServerId = std::string{server.Id},
								  .RuntimeMajor = server.Runtime.MinimumMajor,
								  .Message = std::format("{} support through {} needs .NET {}, which was not found.", LanguageDisplayName(language),
														 server.DisplayName, server.Runtime.MinimumMajor),
								  .Buttons = {LanguageBarChoice::GetRuntime, LanguageBarChoice::OpenSettings, LanguageBarChoice::NotNow}};
		}
	}

	std::optional<LanguagePrompt> LanguagePromptFor(const LanguagePromptInputs& inputs)
	{
		if (inputs.RestrictedModeShown || inputs.Dismissed || !LanguageHasServers(inputs.Language))
		{
			return std::nullopt;
		}

		switch (inputs.Choice.Kind)
		{
		case ServerChoiceKind::Off:
			return std::nullopt;
		case ServerChoiceKind::NotConfigured:
			return LanguagePrompt{.Language = inputs.Language,
								  .Message = std::format("No {} language support. Choose a language server to enable go to definition, "
														 "completion and diagnostics.",
														 LanguageDisplayName(inputs.Language)),
								  .Buttons = {LanguageBarChoice::Configure, LanguageBarChoice::NotNow}};
		case ServerChoiceKind::Chosen:
			break;
		}

		const ServerDefinition& server = *inputs.Choice.Server;
		const InstallSummary summary = SummariseInstall(inputs.Install);

		if (inputs.Install.Running == InstallJobKind::Install && inputs.State == ServerState::NotInstalled)
		{
			return InstallingPrompt(inputs.Language, server, summary);
		}

		// An uninstall or a check is Settings' to show: the bar would offer an install of what is being removed.
		if (inputs.Install.Running.has_value())
		{
			return std::nullopt;
		}

		switch (inputs.State)
		{
		case ServerState::NotInstalled:
			return NotInstalledPrompt(inputs.Language, server, summary);
		case ServerState::MissingRuntime:
			return MissingRuntimePrompt(inputs.Language, server);
		case ServerState::NotConfigured:
		case ServerState::Off:
		case ServerState::NotFound:
		case ServerState::Starting:
		case ServerState::Indexing:
		case ServerState::ProjectIncomplete:
		case ServerState::Ready:
		case ServerState::Failed:
		case ServerState::Restricted:
			break;
		}

		return std::nullopt;
	}

	const char* LanguageBarChoiceLabel(LanguageBarChoice choice)
	{
		switch (choice)
		{
		case LanguageBarChoice::None:
			return "";
		case LanguageBarChoice::Configure:
			return "Configure";
		case LanguageBarChoice::Install:
			return "Install";
		case LanguageBarChoice::Cancel:
			return "Cancel";
		case LanguageBarChoice::GetRuntime:
			return "Get .NET";
		case LanguageBarChoice::OpenSettings:
			return "Settings";
		case LanguageBarChoice::NotNow:
			break;
		}

		return "Not now";
	}

	LanguageBarChoice DrawLanguageSupportBar(const LanguagePrompt& prompt)
	{
		LanguageBarChoice clicked = LanguageBarChoice::None;

		const ImGuiStyle& style = ImGui::GetStyle();
		const float height = ImGui::GetFrameHeight() + style.FramePadding.y * 2.0f;

		ImGui::PushStyleColor(ImGuiCol_ChildBg, ImGui::GetStyleColorVec4(ImGuiCol_MenuBarBg));
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(style.ItemSpacing.x, style.FramePadding.y));

		if (ImGui::BeginChild("##LanguageSupport", ImVec2(0.0f, height), ImGuiChildFlags_AlwaysUseWindowPadding,
							  ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse))
		{
			const std::string text = std::string{ICON_MDI_INFORMATION_OUTLINE} + "  " + prompt.Message;

			ImGui::AlignTextToFramePadding();
			ImGui::TextUnformatted(text.c_str());

			for (const LanguageBarChoice button : prompt.Buttons)
			{
				ImGui::SameLine();

				if (ImGui::Button(LanguageBarChoiceLabel(button)))
				{
					clicked = button;
				}
			}
		}

		ImGui::EndChild();
		ImGui::PopStyleVar();
		ImGui::PopStyleColor();

		return clicked;
	}
}
