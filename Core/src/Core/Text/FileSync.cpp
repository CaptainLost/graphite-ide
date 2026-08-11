#include "corepch.h"

#include "Core/Text/FileSync.h"

#include "Core/Fs/FileSystem.h"
#include "Core/Text/Caret.h"
#include "Core/Text/Columns.h"
#include "Core/Text/DocumentSet.h"
#include "Core/Text/Editing.h"
#include "Core/Text/TextDocument.h"

#include <system_error>

namespace core::text
{
    FileSyncOutcome SyncWithFile(DocumentSet& documents, std::size_t index)
    {
        OpenItem& item = documents.At(index);

        std::error_code error;
        const bool present = std::filesystem::exists(item.path, error);

        bool cameBack = false;

        if (item.failure == OpenFailure::Missing)
        {
            // An error is not an answer, the same as below: a share that blinked must not
            // read as "still gone".
            if (error || !present)
            {
                return FileSyncOutcome::Unchanged;
            }

            documents.MarkPresent(item.path);
            cameBack = true;

            // Deliberately falls through rather than returning. This is the very event
            // saying the file came back, and so the one moment its content is most worth
            // comparing: a file deleted and restored with different content — git checkout
            // after a git rm — would otherwise go on showing the old content until some
            // unrelated later sweep happened to notice. MarkPresent only clears the
            // Missing flag; it does not touch the item's document or its stamp, and the
            // reference above stays valid across it.
        }

        // An error is not an answer. A share that blinked or a directory that briefly
        // denied access must not turn every document open on it into a deleted one — the
        // same reasoning SyncRepository applies before dropping a repository.
        if (!error && !present)
        {
            documents.MarkMissing(item.path);
            return FileSyncOutcome::WentMissing;
        }

        if (!item.document || !item.document->ChangedOnDisk())
        {
            return cameBack ? FileSyncOutcome::CameBack : FileSyncOutcome::Unchanged;
        }

        if (!item.document->IsModified())
        {
            // Not reloaded here — see SafeToReload on why the caller does it.
            return FileSyncOutcome::SafeToReload;
        }

        // Dirty: neither reloaded nor overwritten here. Which version survives is not a
        // decision this can make, so it is handed back as one still to be made.
        return FileSyncOutcome::NeedsDecision;
    }

    Result<void> ReloadItem(DocumentSet& documents, std::size_t index)
    {
        OpenItem& item = documents.At(index);

        // Nothing to reload into. Unreachable from SyncWithFile, which checks first, but
        // this is public API and dereferencing an empty optional is a poor way to find
        // that out.
        if (!item.document)
        {
            return Error{ErrorCode::InvalidArgument,
                         "No document to reload: " + fs::ToUtf8(item.path)};
        }

        if (auto result = item.document->ReloadFromDisk(); !result)
        {
            return result.GetError();
        }

        // The reload can shorten the document past where the caret sat. PlaceAt clamps to
        // the new length and pulls back to a character boundary, the same as a click past
        // a line's end does.
        item.caret =
            PlaceAt(item.document->Buffer(), item.caret, item.caret.offset, false, DefaultTabWidth);
        RefreshDerived(item);

        return {};
    }

    Result<void> SaveDocument(DocumentSet& documents, std::size_t index)
    {
        OpenItem& item = documents.At(index);

        if (!item.document)
        {
            return {};
        }

        if (auto result = item.document->Save(); !result)
        {
            return result.GetError();
        }

        // A document marked Missing whose file this just recreated (Save writes through
        // WriteFileAtomically, which falls through to MoveFileExW onto a path that need
        // not already exist) is still there on disk now — the tab should stop saying
        // "(deleted)". MarkPresent already refuses to touch Binary, TooLarge and
        // ReadFailed, so it is safe to call unconditionally on every successful save
        // rather than only when the failure was Missing.
        documents.MarkPresent(item.path);

        return {};
    }

    SaveAllReport SaveAllModified(DocumentSet& documents)
    {
        SaveAllReport report;

        for (std::size_t index = 0; index < documents.Count(); ++index)
        {
            const OpenItem& item = documents.At(index);

            if (!item.document || !item.document->IsModified())
            {
                continue;
            }

            // The same safety net a single deliberate save gets: ChangedOnDisk() true
            // means a change nobody was told about. But unlike a single save, a batch has
            // no good way to ask about it mid-flight without turning into a sequence of
            // modals — so this document is skipped, not asked about and not silently
            // overwritten, and the batch continues: three other dirty files having nothing
            // wrong with them is no reason to leave them unsaved over a fourth that does.
            if (item.document->ChangedOnDisk())
            {
                report.conflicted.push_back(item.path);
                continue;
            }

            // A genuine write failure is different from a conflict: it stops the batch on
            // the spot rather than being collected, since continuing past a real I/O error
            // would be a batch pretending nothing went wrong.
            if (auto result = SaveDocument(documents, index); !result)
            {
                report.stoppedAt = SaveAllReport::Failure{item.path, result.GetError()};
                return report;
            }

            ++report.saved;
        }

        return report;
    }
}
