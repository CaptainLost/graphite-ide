#include "UI/InfoBars/LanguageSupportBar.h"

#include "UI/InfoBars/InfoBar.h"

#include "Language/Registry/LanguageRegistry.h"

#include <IconsMaterialDesignIcons.h>

#include <imgui.h>

#include <format>

namespace Graphite {

	namespace {

		bool IsMainAction(LanguageBarChoice choice)
		{
			switch (choice)
			{
			case LanguageBarChoice::Configure:
			case LanguageBarChoice::Install:
			case LanguageBarChoice::GetRuntime:
				return true;
			case LanguageBarChoice::None:
			case LanguageBarChoice::Cancel:
			case LanguageBarChoice::OpenSettings:
			case LanguageBarChoice::NotNow:
				break;
			}

			return false;
		}

		bool Offers(const InstallSummary& summary, InstallAction action)
		{
			for (const InstallAction offered : summary.Actions)
			{
				if (offered == action)
				{
					return true;
				}
			}

			return false;
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

		const bool settingsShowsRunningJob = inputs.Install.Running.has_value();

		if (settingsShowsRunningJob)
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
		InfoBarContent content{.Id = "##LanguageSupport",
							   .Weight = InfoBarWeight::Information,
							   .Glyph = ICON_MDI_INFORMATION_OUTLINE,
							   .WeightColour = ImGui::GetStyleColorVec4(ImGuiCol_CheckMark),
							   .Message = prompt.Message};

		std::vector<LanguageBarChoice> shown;

		for (const LanguageBarChoice choice : prompt.Buttons)
		{
			if (choice == LanguageBarChoice::NotNow)
			{
				content.CloseTooltip = LanguageBarChoiceLabel(choice);
				continue;
			}

			content.Buttons.push_back({.Label = LanguageBarChoiceLabel(choice), .Primary = IsMainAction(choice)});
			shown.push_back(choice);
		}

		const InfoBarAnswer answer = DrawInfoBar(content);

		if (answer.Closed)
		{
			return LanguageBarChoice::NotNow;
		}

		return answer.Pressed.has_value() ? shown[*answer.Pressed] : LanguageBarChoice::None;
	}
}
