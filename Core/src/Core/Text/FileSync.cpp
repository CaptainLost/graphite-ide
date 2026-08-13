#include "corepch.h"

#include "Core/Text/FileSync.h"

#include "Core/Fs/FileSystem.h"
#include "Core/Text/Caret.h"
#include "Core/Text/Columns.h"
#include "Core/Text/DocumentSet.h"
#include "Core/Text/Editing.h"
#include "Core/Text/TextDocument.h"

#include <system_error>

namespace Core {

	FileSyncOutcome SyncWithFile(DocumentSet& documents, std::size_t index)
	{
		OpenItem& item = documents.At(index);

		std::error_code error;
		const bool present = std::filesystem::exists(item.Path, error);

		// A stat error is not an answer: a share that blinked must read as neither "still
		// gone" above nor "deleted" below.
		const bool knownGone = !error && !present;
		const bool knownPresent = !error && present;

		bool cameBack = false;

		if (item.Failure == OpenFailure::Missing)
		{
			if (!knownPresent)
				return FileSyncOutcome::Unchanged;

			documents.MarkPresent(item.Path);
			cameBack = true;

			// Falls through rather than returning: a file deleted and restored with different
			// content would otherwise go on showing the old content until some later sweep
			// happened to notice.
		}

		if (knownGone)
		{
			documents.MarkMissing(item.Path);
			return FileSyncOutcome::WentMissing;
		}

		if (!item.Document || !item.Document->CheckChangedOnDisk())
		{
			// Only the item that just came back is waiting for its file. Binary, TooLarge and
			// ReadFailed also carry no document, but they mean "not opening this".
			return cameBack ? FileSyncOutcome::NeedsLoad : FileSyncOutcome::Unchanged;
		}

		if (!item.Document->IsModified())
		{
			// Not reloaded here — see SafeToReload on why the caller does it.
			return FileSyncOutcome::SafeToReload;
		}

		return FileSyncOutcome::NeedsDecision;
	}

	Result<void> ReloadItem(DocumentSet& documents, std::size_t index)
	{
		OpenItem& item = documents.At(index);

		if (!item.Document)
		{
			return Error{ErrorCode::InvalidArgument,
						 "No document to reload: " + ToUtf8(item.Path)};
		}

		if (auto result = item.Document->ReloadFromDisk(); !result)
		{
			return result.GetError();
		}

		// The reload can shorten the document past where the caret sat; PlaceAt clamps to the
		// new length and pulls back to a character boundary.
		item.Caret =
			PlaceAt(item.Document->GetBuffer(), item.Caret, item.Caret.Offset, false, DefaultTabWidth);

		// The vertical aim was measured against text that has gone. Cleared rather than
		// converted, so the next Up or Down resolves it from where the caret actually is now.
		item.RowAimAt = NoRowAim;

		RefreshDerived(item);

		return {};
	}

	Result<void> SaveDocument(DocumentSet& documents, std::size_t index)
	{
		OpenItem& item = documents.At(index);

		if (!item.Document)
		{
			return {};
		}

		if (auto result = item.Document->Save(); !result)
		{
			return result.GetError();
		}

		// A save recreates the file of a document marked Missing, so the tab should stop
		// saying "(deleted)". MarkPresent refuses to touch any other failure, so it is safe
		// on every successful save rather than only on that one.
		documents.MarkPresent(item.Path);

		return {};
	}

	SaveAllReport SaveAllModified(DocumentSet& documents)
	{
		SaveAllReport report;

		for (std::size_t index = 0; index < documents.GetCount(); ++index)
		{
			const OpenItem& item = documents.At(index);

			if (!item.Document || !item.Document->IsModified())
			{
				continue;
			}

			// A batch has no way to ask about a change nobody was told about without turning
			// into a sequence of modals, so this document is collected and skipped rather
			// than silently overwritten, and the rest of the batch still gets saved.
			if (item.Document->CheckChangedOnDisk())
			{
				report.Conflicted.push_back(item.Path);
				continue;
			}

			// A write failure stops the batch rather than being collected: continuing past a
			// real I/O error would be a batch pretending nothing went wrong.
			if (auto result = SaveDocument(documents, index); !result)
			{
				report.StoppedAt = SaveAllReport::Failure{item.Path, result.GetError()};
				return report;
			}

			++report.Saved;
		}

		return report;
	}
}
