#include "Language/Diagnostics/ProblemsChecking.h"

#include "Language/Registry/LanguageRegistry.h"
#include "Language/Server/ServerWaitingText.h"
#include "UI/TypographicMarks.h"

#include "Core/Base/Strings.h"
#include "Core/FileSystem/FileSystem.h"

#include <algorithm>

namespace Graphite {

	namespace {
		constexpr std::size_t kNamesListed = 3;

		bool IsAnswered(const DiagnosticsModel::Entry& entry)
		{
			return entry.HasAnswer && !(entry.Answered < entry.Sent);
		}

		bool ServerGaveUpOnLatest(const DiagnosticsModel::Entry& entry)
		{
			return entry.GaveUpOn != DocumentVersion{} && !(entry.GaveUpOn < entry.Sent);
		}

		std::string UncheckedLanguageText(const OpenDocumentCheck& document)
		{
			return std::string{LanguageDisplayName(document.Language)} + " files are not checked: " +
				   ServerUnavailableText(document.Server);
		}

		void AddOnce(std::vector<std::string>& lines, std::string line)
		{
			if (std::find(lines.begin(), lines.end(), line) == lines.end())
			{
				lines.push_back(std::move(line));
			}
		}

		std::string WaitingForServerPart(const ProblemsChecking& checking)
		{
			return ServerWaitingText(*checking.WaitingFor) + std::string{kMiddleDotSeparator} +
				   Core::Counted(static_cast<long long>(checking.DocumentsWaitingForServer), "open file") + " not checked yet";
		}
	}

	DocumentCheck CheckOf(const OpenDocumentCheck& document)
	{
		if (!LanguageHasServers(document.Language))
		{
			return DocumentCheck::NotSupported;
		}

		const DiagnosticsModel::Entry* const entry = document.Entry;

		if (entry != nullptr && IsAnswered(*entry))
		{
			return DocumentCheck::Checked;
		}

		switch (AvailabilityOf(document.Server))
		{
		case ServerAvailability::Unavailable:
			return DocumentCheck::ServerUnavailable;

		case ServerAvailability::ComingUp:
			return DocumentCheck::WaitingForServer;

		case ServerAvailability::Answers:
			break;
		}

		if (entry == nullptr)
		{
			return DocumentCheck::NeverSent;
		}

		return ServerGaveUpOnLatest(*entry) ? DocumentCheck::ServerGaveNoAnswer : DocumentCheck::Checking;
	}

	bool ProblemsChecking::EverythingChecked() const
	{
		return !AnyNotes() && !AnyStillComing();
	}

	bool ProblemsChecking::AnyStillComing() const
	{
		return !StillChecking.empty() || DocumentsWaitingForServer > 0;
	}

	bool ProblemsChecking::AnyNotes() const
	{
		return !UncheckedLanguages.empty() || !NotAnswered.empty() || !NeverSent.empty();
	}

	ProblemsChecking SummariseChecking(const std::vector<OpenDocumentCheck>& documents)
	{
		ProblemsChecking checking;

		for (const OpenDocumentCheck& document : documents)
		{
			switch (CheckOf(document))
			{
			case DocumentCheck::Checked:
				checking.AnyAnswered = true;
				break;

			case DocumentCheck::Checking:
				checking.StillChecking.push_back(document.Path);
				checking.AnyAnswered = checking.AnyAnswered || document.Entry->HasAnswer;
				break;

			case DocumentCheck::WaitingForServer:
				++checking.DocumentsWaitingForServer;

				if (!checking.WaitingFor)
				{
					checking.WaitingFor = document.Server;
				}
				break;

			case DocumentCheck::ServerUnavailable:
				AddOnce(checking.UncheckedLanguages, UncheckedLanguageText(document));
				break;

			case DocumentCheck::ServerGaveNoAnswer:
				checking.NotAnswered.push_back(UncheckedDocument{.Path = document.Path, .Server = document.Server});
				break;

			case DocumentCheck::NeverSent:
				checking.NeverSent.push_back(document.Path);
				break;

			case DocumentCheck::NotSupported:
				break;
			}
		}

		return checking;
	}

	std::string FileNamesText(const std::vector<std::filesystem::path>& paths)
	{
		std::vector<std::string> names;

		for (const std::filesystem::path& path : paths)
		{
			if (names.size() == kNamesListed)
			{
				break;
			}

			names.push_back(Core::ToUtf8(path.filename()));
		}

		std::string text = Core::JoinedWith(names, ", ");

		if (paths.size() > kNamesListed)
		{
			text += " and " + std::to_string(paths.size() - kNamesListed) + " more";
		}

		return text;
	}

	std::string StillCheckingText(const ProblemsChecking& checking)
	{
		std::string text;

		if (!checking.StillChecking.empty())
		{
			text = "Still checking " + FileNamesText(checking.StillChecking);
		}

		if (checking.WaitingFor)
		{
			const std::string waiting = WaitingForServerPart(checking);
			text = text.empty() ? AsSentence(waiting, *checking.WaitingFor) : text + std::string{kMiddleDotSeparator} + waiting;
		}

		return text;
	}

	std::string NoProblemsSoFarText(const ProblemsChecking& checking)
	{
		std::string text = "No problems so far";

		if (!checking.StillChecking.empty())
		{
			text += std::string{kMiddleDotSeparator} + "checking " + FileNamesText(checking.StillChecking) + std::string{kEllipsis};
		}

		if (checking.WaitingFor)
		{
			text += std::string{kMiddleDotSeparator} + WaitingForServerPart(checking);
		}

		return text;
	}

	std::string NothingCheckedYetText(const ProblemsChecking& checking)
	{
		if (!checking.WaitingFor)
		{
			return "Checking " + FileNamesText(checking.StillChecking);
		}

		ProblemsChecking everyoneWaiting = checking;
		everyoneWaiting.DocumentsWaitingForServer += checking.StillChecking.size();

		return AsSentence(WaitingForServerPart(everyoneWaiting), *checking.WaitingFor);
	}

	std::string NotAnsweredText(const UncheckedDocument& document)
	{
		return Core::ToUtf8(document.Path.filename()) + " was not checked: " + ServerNameOf(document.Server) +
			   " did not answer";
	}

	std::string NeverSentText(const ProblemsChecking& checking)
	{
		return "Not checked until shown: " + FileNamesText(checking.NeverSent);
	}
}
