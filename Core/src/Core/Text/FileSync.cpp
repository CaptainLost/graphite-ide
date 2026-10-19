#include "corepch.h"

#include "Core/Text/FileSync.h"

#include "Core/FileSystem/FileSystem.h"
#include "Core/Text/Caret.h"
#include "Core/Text/Columns.h"
#include "Core/Text/DocumentSet.h"
#include "Core/Text/Editing.h"
#include "Core/Text/TextDocument.h"

#include <system_error>

namespace Core {

	FileSyncOutcome SyncWithFile(DocumentSet& documents, std::size_t itemIndex)
	{
		OpenItem& item = documents.At(itemIndex);

		std::error_code existsError;
		const bool present = std::filesystem::exists(item.Path, existsError);

		// A failed stat is not an answer: a share that blinked must neither mark an open document
		// deleted nor bring a missing one back. Nothing portable can make exists() fail, so no test pins this.
		const bool knownGone = !existsError && !present;
		const bool knownPresent = !existsError && present;

		bool cameBack = false;

		if (item.Failure == OpenFailure::Missing)
		{
			if (!knownPresent)
			{
				return FileSyncOutcome::Unchanged;
			}

			documents.MarkPresent(item.Path);
			cameBack = true;
		}

		if (knownGone)
		{
			documents.MarkMissing(item.Path);
			return FileSyncOutcome::WentMissing;
		}

		if (!item.Document || !item.Document->CheckChangedOnDisk())
		{
			return cameBack ? FileSyncOutcome::NeedsLoad : FileSyncOutcome::Unchanged;
		}

		if (!item.Document->IsModified())
		{
			return FileSyncOutcome::SafeToReload;
		}

		return FileSyncOutcome::NeedsDecision;
	}

	Result<void> ReloadItem(DocumentSet& documents, std::size_t itemIndex)
	{
		OpenItem& item = documents.At(itemIndex);

		if (!item.Document)
		{
			return Error{.Code = ErrorCode::InvalidArgument, .Message = "No document to reload: " + ToUtf8(item.Path)};
		}

		if (const Result<void> reloaded = item.Document->ReloadFromDisk(); !reloaded)
		{
			return reloaded.GetError();
		}

		CatchUpWithDocument(item, CaretFollow::Shift);

		return {};
	}

	Result<void> SaveDocument(DocumentSet& documents, std::size_t itemIndex)
	{
		OpenItem& item = documents.At(itemIndex);

		if (!item.Document)
		{
			return {};
		}

		if (const Result<void> saved = item.Document->Save(); !saved)
		{
			return saved.GetError();
		}

		documents.MarkPresent(item.Path);

		return {};
	}

	SaveAllReport SaveAllModified(DocumentSet& documents)
	{
		SaveAllReport report;

		for (const std::shared_ptr<TextDocument>& document : documents.GetStore().GetDocuments())
		{
			if (!document->IsModified())
			{
				continue;
			}

			if (document->CheckChangedOnDisk())
			{
				report.Conflicted.push_back(document->GetPath());
				continue;
			}

			if (const Result<void> saved = document->Save(); !saved)
			{
				report.StoppedAt = SaveAllReport::Failure{.Path = document->GetPath(), .Error = saved.GetError()};
				return report;
			}

			documents.MarkPresent(document->GetPath());
			report.Saved.push_back(document->GetPath());
		}

		return report;
	}
}
