#include "Editor/StatusBar/StatusLineComposer.h"

#include "Language/Registry/LanguageRegistry.h"

namespace Graphite {

	namespace {

		std::optional<LanguageId> SettingsOf(LanguageId language)
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
				const std::string percent = job.Percent ? " " + std::to_string(*job.Percent) + "%" : std::string{};

				return ServerLine{.Text = prefix + "installing " + job.Server + percent,
								  .Detail = "Graphite is downloading and installing " + job.Server + ". It starts as soon as it is in place.",
								  .SettingsLanguage = SettingsOf(language)};
			}
			case InstallJobKind::Uninstall:
				return ServerLine{.Text = prefix + "uninstalling " + job.Server,
								  .Detail = "Graphite is removing its install of " + job.Server + ".",
								  .SettingsLanguage = SettingsOf(language)};
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
			problems = std::to_string(errors) + (errors == 1 ? " error" : " errors");
		}

		if (warnings > 0)
		{
			if (!problems.empty())
			{
				problems += ", ";
			}

			problems += std::to_string(warnings) + (warnings == 1 ? " warning" : " warnings");
		}

		return problems;
	}

	std::optional<ServerLine> ChooseServerLine(const ServerLineInputs& inputs)
	{
		if (inputs.ActiveJob)
		{
			if (std::optional<ServerLine> job = JobLine(inputs.ActiveLanguage, *inputs.ActiveJob))
			{
				return job;
			}
		}

		if (inputs.Active.State != ServerState::NotConfigured)
		{
			// The active document's own language has something to say, and it outranks everything
			// else: it is the one whose answers are about what is on screen.
			//
			// Named while its answers are still coming, because a ready server and a file whose
			// colours have not arrived look identical from here otherwise - and that gap is
			// exactly when someone wonders whether anything is happening at all.
			return ServerLine{.Text = inputs.AnalysingFile
										  ? AnalysingText(inputs.Active, *inputs.AnalysingFile)
										  : ServerStatusText(inputs.Active),
							  .Detail = ServerStatusDetail(inputs.Active),
							  .SettingsLanguage = SettingsOf(inputs.Active.Language)};
		}

		if (std::string unserved = NoServerText(inputs.ActiveLanguage); !unserved.empty())
		{
			// A language recognised but unserved - a C# file with no OmniSharp. Said out loud,
			// because the expectation of go to definition is reasonable here and silence would
			// read as something broken rather than something absent.
			return ServerLine{.Text = std::move(unserved),
							  .Detail = NoServerDetail(inputs.ActiveLanguage),
							  .SettingsLanguage = SettingsOf(inputs.ActiveLanguage)};
		}

		if (inputs.FolderOpen && !inputs.FolderTrusted)
		{
			// Ahead of the workspace-wide status below, not after it: that one reports the best
			// server anywhere, which may belong to a trusted folder whose tabs are still open, and
			// "ready" beside the restricted-mode bar reads as the bar being wrong.
			return ServerLine{
				.Text = "Restricted mode",
				.Detail = "This folder is not trusted, so the language server and the background analysis "
						  "do not run in it. Trust it from the bar above the editor or the File menu."};
		}

		if (inputs.Overall && inputs.FolderOpen)
		{
			// Nothing to say about what is open - plain text, or no document at all - so the
			// workspace's servers as a whole. This is what makes a server started by opening the
			// folder visible at all: without it the bar stays blank until a file of its language
			// is opened, which looks like the feature is not working. Only with a folder open: a
			// server left over from the folder just closed is not the workspace's.
			return ServerLine{.Text = ServerStatusText(*inputs.Overall),
							  .Detail = ServerStatusDetail(*inputs.Overall),
							  .SettingsLanguage = SettingsOf(inputs.Overall->Language)};
		}

		return std::nullopt;
	}
}
