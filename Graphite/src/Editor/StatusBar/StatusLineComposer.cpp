#include "Editor/StatusBar/StatusLineComposer.h"

#include "Language/Registry/LanguageRegistry.h"

#include "Core/Base/Strings.h"

namespace Graphite {

	namespace {

		std::optional<LanguageId> SettingsLanguageIfServed(LanguageId language)
		{
			if (!LanguageHasServers(language))
			{
				return std::nullopt;
			}

			return language;
		}

		std::optional<ServerLine> JobLine(LanguageId language, const ServerJob& job)
		{
			const std::string prefix = std::string{LanguageDisplayName(language)} + ": ";

			switch (job.Kind)
			{
			case InstallJobKind::Install:
			{
				const std::string percentSuffix = job.Percent ? " " + std::to_string(*job.Percent) + "%" : std::string{};

				return ServerLine{.Text = prefix + "installing " + job.Server + percentSuffix,
								  .Detail = "Graphite is downloading and installing " + job.Server + ". It starts as soon as it is in place.",
								  .SettingsLanguage = SettingsLanguageIfServed(language)};
			}
			case InstallJobKind::Uninstall:
				return ServerLine{.Text = prefix + "uninstalling " + job.Server,
								  .Detail = "Graphite is removing its install of " + job.Server + ".",
								  .SettingsLanguage = SettingsLanguageIfServed(language)};
			case InstallJobKind::CheckForUpdates:
				break;
			}

			return std::nullopt;
		}
	}

	std::optional<std::string> ProblemCountText(std::size_t errors, std::size_t warnings)
	{
		if (errors == 0 && warnings == 0)
		{
			return std::nullopt;
		}

		std::string problems;

		if (errors > 0)
		{
			problems = Core::Counted(errors, "error");
		}

		if (warnings > 0)
		{
			if (!problems.empty())
			{
				problems += ", ";
			}

			problems += Core::Counted(warnings, "warning");
		}

		return problems;
	}

	std::optional<ServerLine> ChooseServerLine(const ServerLineInputs& inputs)
	{
		if (inputs.ActiveJob)
		{
			std::optional<ServerLine> jobLine = JobLine(inputs.ActiveLanguage, *inputs.ActiveJob);

			if (jobLine)
			{
				return jobLine;
			}
		}

		const bool activeDocumentHasServer = inputs.Active.State != ServerState::NotConfigured;

		if (activeDocumentHasServer)
		{
			const std::string activeText = inputs.AnalysingFile ? AnalysingText(inputs.Active, *inputs.AnalysingFile)
																: ServerStatusText(inputs.Active);

			return ServerLine{.Text = activeText,
							  .Detail = ServerStatusDetail(inputs.Active),
							  .SettingsLanguage = SettingsLanguageIfServed(inputs.Active.Language)};
		}

		if (std::string unserved = NoServerText(inputs.ActiveLanguage); !unserved.empty())
		{
			return ServerLine{.Text = std::move(unserved),
							  .Detail = NoServerDetail(inputs.ActiveLanguage),
							  .SettingsLanguage = SettingsLanguageIfServed(inputs.ActiveLanguage)};
		}

		const bool folderIsRestricted = inputs.FolderOpen && !inputs.FolderTrusted;

		if (folderIsRestricted)
		{
			return ServerLine{
				.Text = "Restricted mode",
				.Detail = "This folder is not trusted, so the language server and the background analysis "
						  "do not run in it. Trust it from the bar above the editor or the File menu."};
		}

		if (inputs.Overall && inputs.FolderOpen)
		{
			return ServerLine{.Text = ServerStatusText(*inputs.Overall),
							  .Detail = ServerStatusDetail(*inputs.Overall),
							  .SettingsLanguage = SettingsLanguageIfServed(inputs.Overall->Language)};
		}

		return std::nullopt;
	}
}
