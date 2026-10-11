#include "Editor/StatusBar/ServerStatusText.h"

#include "Language/Registry/LanguageRegistry.h"
#include "UI/TypographicMarks.h"

#include "Core/Base/ByteSize.h"

namespace Graphite {

	namespace {

		// "restarted N times" once there has been at least one, empty otherwise — a server that
		// has restarted is still working, but the user should know it has been unhappy.
		std::string RestartNote(const ServerStatus& status)
		{
			if (status.Restarts == 0)
			{
				return {};
			}

			return "restarted " + std::to_string(status.Restarts) +
				   (status.Restarts == 1 ? " time" : " times");
		}

		std::string LanguagePrefix(LanguageId language)
		{
			const std::string_view name = LanguageDisplayName(language);

			return name.empty() ? std::string{} : std::string{name} + ": ";
		}

		std::string Phrase(const ServerStatus& status, std::string_view words)
		{
			return LanguagePrefix(status.Language) + status.Name + " " + std::string{words};
		}

		std::string SetApart(const ServerStatus& status, std::string_view words)
		{
			return LanguagePrefix(status.Language) + status.Name + std::string{kEmDashSeparator} + std::string{words};
		}

	}

	std::string ServerStatusText(const ServerStatus& status)
	{
		switch (status.State)
		{
		case ServerState::NotConfigured:
			return {};

		case ServerState::Off:
			return LanguagePrefix(status.Language) + "language server off";

		case ServerState::NotFound:
			return Phrase(status, "not found at the configured path");

		case ServerState::NotInstalled:
			return Phrase(status, "not installed");

		case ServerState::MissingRuntime:
			return Phrase(status, status.Detail);

		case ServerState::Restricted:
			return SetApart(status, "restricted mode");

		case ServerState::Starting:
			return Phrase(status, "starting");

		case ServerState::Indexing:
			// Percent is -1 for "not reported": a 0% while a server starts up would be a worse lie than no number.
			if (status.Percent >= 0)
			{
				return Phrase(status, "indexing " + std::to_string(status.Percent) + "%");
			}

			if (!status.Loading.empty())
			{
				return Phrase(status, "loading " + status.Loading);
			}

			return Phrase(status, "indexing");

		case ServerState::ProjectIncomplete:
			return SetApart(status, status.Problem.Summary);

		case ServerState::Ready:
			return Phrase(status, "ready");

		case ServerState::Failed:
			return Phrase(status, "failed");
		}

		return {};
	}

	std::string AnalysingText(const ServerStatus& status, const std::string& fileName)
	{
		if (status.State != ServerState::Ready)
		{
			return ServerStatusText(status);
		}

		return Phrase(status, "analysing " + fileName);
	}

	std::string NoServerText(LanguageId language)
	{
		const std::string_view name = LanguageDisplayName(language);

		if (name.empty())
		{
			return {};
		}

		return std::string{name} + (LanguageHasServers(language) ? ": no language support" : ": no language server");
	}

	std::string NoServerDetail(LanguageId language)
	{
		const std::string name{LanguageDisplayName(language)};

		if (name.empty())
		{
			return {};
		}

		if (LanguageHasServers(language))
		{
			return "No language server is chosen for " + name +
				   ", so go to definition, completion and diagnostics are unavailable here. Choose one in Settings, under "
				   "Languages. Syntax highlighting does not depend on a server and is unaffected.";
		}

		return "No language server is configured for " + name +
			   ", so go to definition and reference counts are unavailable here. Syntax highlighting does not "
			   "depend on a server and is unaffected.";
	}

	std::string ServerStatusDetail(const ServerStatus& status)
	{
		std::string detail;

		switch (status.State)
		{
		case ServerState::NotConfigured:
			break;

		case ServerState::Off:
			detail = "The language server for " + status.Name + " is turned off in the settings.";
			break;

		case ServerState::NotFound:
			detail = status.Detail.empty() ? status.Name + " was not found at the path configured for it."
										   : status.Name + " was not found: " + status.Detail + ".";
			break;

		case ServerState::NotInstalled:
			detail = status.Detail.empty() ? status.Name + " is not installed."
										   : status.Name + " is not installed: " + status.Detail + ".";
			break;

		case ServerState::MissingRuntime:
			detail = status.Name + " " + status.Detail +
					 " or newer, and no such runtime was found in DOTNET_ROOT, beside dotnet on PATH or under Program Files.";
			break;

		case ServerState::ProjectIncomplete:
			detail = status.Problem.Explanation;
			break;

		case ServerState::Restricted:
			// Detail holds the server's root. It is named because it need not be the open folder -
			// it can lie above it, or be another folder a single file was opened from - and the
			// remedy is phrased to stay true when the bar is not there to offer it.
			if (status.Detail.empty())
			{
				detail = "This folder is not trusted, so " + status.Name +
						 " does not run for it. Trust the folder from the bar above the editor or "
						 "the File menu to start it.";
			}
			else
			{
				// "With it open" rather than a pointer at the bar alone: a file opened with no folder,
				// or from outside the open one, has no bar offering its own folder.
				detail = "The folder " + status.Detail + " is not trusted, so " + status.Name +
						 " does not run for it. With it, or a folder above it, open as the workspace, "
						 "trust it from the bar above the editor or the File menu.";
			}
			break;

		case ServerState::Starting:
		case ServerState::Indexing:
		case ServerState::Ready:
		case ServerState::Failed:
			// The command while starting, the version the handshake reported once it is up.
			detail = status.Detail;
			break;
		}

		if (const std::string restarts = RestartNote(status); !restarts.empty())
		{
			if (!detail.empty())
			{
				detail += "  ";
			}

			detail += restarts;
		}

		return detail;
	}

	std::string BackgroundAnalysisText(const AnalysisProgress& progress)
	{
		if (progress.Walking)
		{
			return "analysing workspace: finding files";
		}

		// Total == 0 is the pass having nothing queued yet; Done >= Total (once Total is
		// non-zero) is the pass having finished. Both are the resting state and both say
		// nothing, but they are different reasons, which is why the check names them separately
		// rather than folding them into one comparison a reader would have to unpick.
		if (progress.Total == 0 || progress.Done >= progress.Total)
		{
			return {};
		}

		return "analysing workspace: " + std::to_string(progress.Done) + " of " +
			   std::to_string(progress.Total);
	}

	std::string BackgroundAnalysisDetail(const AnalysisProgress& progress, std::size_t keptBytes,
										 std::size_t budgetBytes)
	{
		std::string detail = "Reading the workspace so a file opens already coloured: " +
							 std::to_string(progress.Done) + " of " +
							 std::to_string(progress.Total) + " files.\n";

		detail += "Holding " + Core::FormatWholeMegabytes(keptBytes) + " of " + Core::FormatWholeMegabytes(budgetBytes) + ".\n";
		detail += "The file you are reading has its own questions answered first; this waits.";

		return detail;
	}

}
