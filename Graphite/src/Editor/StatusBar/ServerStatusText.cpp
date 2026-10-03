#include "Editor/StatusBar/ServerStatusText.h"

#include "Language/Registry/LanguageRegistry.h"

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

	}

	std::string ServerStatusText(const ServerStatus& status)
	{
		switch (status.State)
		{
		case ServerState::NotConfigured:
			return {};

		case ServerState::NotFound:
			return status.Name + ": not found";

		case ServerState::Restricted:
			return status.Name + ": restricted mode";

		case ServerState::Starting:
			return status.Name + ": starting";

		case ServerState::Indexing:
			// Percent is -1 for "not reported" — a hard-coded 0% while a server starts up would
			// be a worse lie than saying nothing.
			if (status.Percent >= 0)
			{
				return status.Name + ": indexing " + std::to_string(status.Percent) + "%";
			}
			return status.Name + ": indexing";

		case ServerState::NoCompileDatabase:
			return status.Name + ": no compile database";

		case ServerState::Ready:
			return status.Name + ": ready";

		case ServerState::Failed:
			return status.Name + ": failed";
		}

		return {};
	}

	std::string AnalysingText(const ServerStatus& status, const std::string& fileName)
	{
		if (status.State != ServerState::Ready)
		{
			return ServerStatusText(status);
		}

		return status.Name + ": analysing " + fileName;
	}

	std::string NoServerText(LanguageId language)
	{
		const std::string_view name = LanguageDisplayName(language);

		return name.empty() ? std::string{} : std::string{name} + ": no language server";
	}

	std::string NoServerDetail(LanguageId language)
	{
		const std::string_view name = LanguageDisplayName(language);

		if (name.empty())
		{
			return {};
		}

		// Says what is absent and what still works, because the question this answers is "why is
		// nothing happening in this file" and half an answer invites a hunt for a broken setting.
		return "No language server is configured for " + std::string{name} +
			   ", so go to definition and reference counts are unavailable here. Syntax "
			   "highlighting does not depend on a server and is unaffected.";
	}

	std::string ServerStatusDetail(const ServerStatus& status)
	{
		std::string detail;

		switch (status.State)
		{
		case ServerState::NotConfigured:
			break;

		case ServerState::NotFound:
			detail = status.Name +
					 " was not found beside the application, on PATH, or at a configured "
					 "location.";
			break;

		case ServerState::NoCompileDatabase:
			detail = "No compile_commands.json, compile_flags.txt or .clangd was found for this "
					 "project, so " +
					 status.Name +
					 " can see only the files that are open rather than the whole project.";
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
			// The binary path while starting, the version the handshake reported once it is up.
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
