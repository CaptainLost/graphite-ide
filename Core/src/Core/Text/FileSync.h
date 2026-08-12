#pragma once

#include "Core/Base/Result.h"

#include <cstddef>
#include <filesystem>
#include <optional>
#include <vector>

namespace Core
{
    class DocumentSet;

    /// What SyncWithFile found, and — where anything is left to do about it — what the
    /// caller should do.
    ///
    /// The same shape as OpenOutcome and for the same reason: deciding what state a
    /// document and its file are in is policy, but asking a question about it, wording a
    /// message about it and reaching for the file again are not. Handing back the verdict
    /// is what keeps that half outside Core, where nothing can be tested.
    enum class FileSyncOutcome
    {
        /// Nothing to do. Also the answer when the file could not be examined at all —
        /// see SyncWithFile on why a failed stat is never read as an answer.
        Unchanged,

        /// The file was there when the item opened and is not there now. The item was
        /// marked missing and keeps its content: that content is still in memory, so
        /// dropping it would be the only way to lose something we still have.
        WentMissing,

        /// A file that was marked missing is there again, and the item has no content to
        /// show for it: call LoadFromFile.
        ///
        /// This is the tab opened on a file that was not there — a restored session whose
        /// file had been deleted, or a file removed between the tree listing it and the
        /// click. Clearing the missing mark is not enough on its own: it would leave a tab
        /// that no longer says the file is gone, shows nothing, and never loads, since
        /// every later sync sees an item that is neither missing nor holding a document
        /// and reports Unchanged.
        ///
        /// Named for what is left to do rather than for what happened, like SafeToReload
        /// and NeedsDecision beside it. Loading is the caller's because a file that came
        /// back large has to be asked about, and only a caller with a user can ask.
        ///
        /// An item that *does* carry a document never ends here: SyncWithFile compares the
        /// content in the same pass that notices the file returning, deliberately, so a
        /// file that came back changed ends as SafeToReload or NeedsDecision, and one that
        /// came back identical as Unchanged.
        NeedsLoad,

        /// The file changed underneath a document with no unsaved edits, so there is
        /// nothing to weigh against the new content: call ReloadItem.
        ///
        /// The reload is left to the caller rather than done here, even though the
        /// decision to reload was made here. A reload that fails has to be named in a
        /// message, and a caller told only that one failed would have to read the file a
        /// second time to find out why — a question put to a file that has had time to
        /// change its answer. Calling ReloadItem itself, the caller gets the reason from
        /// the Result, which is where a reason belongs.
        SafeToReload,

        /// The file changed underneath a document that has unsaved edits, and nothing was
        /// done about it. Both versions are somebody's work, so only a caller with a user
        /// in front of it can choose between them.
        NeedsDecision
    };

    /// Everything here takes the set and an index, where Editing.h's functions take an
    /// OpenItem&. That difference is deliberate: MarkMissing and MarkPresent are
    /// DocumentSet members that look an item up by path, so reconciling one item against
    /// disk genuinely needs the set it belongs to and not only the item.
    ///
    /// `index` must be below `documents.Count()` in every function here. DocumentSet::At
    /// indexes its vector unchecked, so an index past the end is undefined behaviour
    /// before any of these functions reaches a guard of its own — including ReloadItem's
    /// check for an item carrying no document, which cannot be reached without first
    /// having looked the item up.

    /// Reconciles one open item against its file: whether the file is still there, whether
    /// one that had gone has come back, and whether its content has moved on since it was
    /// read. Marks the item missing or present as it goes, since neither needs anybody's
    /// permission; everything else it hands back rather than does.
    ///
    /// *When* to run this is the caller's business too: only the caller knows which
    /// directories a watcher just named, or that its window has regained focus.
    [[nodiscard]] FileSyncOutcome SyncWithFile(DocumentSet& documents, std::size_t index);

    /// Replaces the item's content with its file's, clamps the caret into the new content
    /// and rebuilds the derived caches. On failure nothing about the document changes and
    /// the error says why, which is what lets the caller name the reason.
    ///
    /// Not called ReloadFromDisk: TextDocument::ReloadFromDisk is what this calls, and one
    /// name at two levels doing two different amounts of work is how a reader ends up
    /// reasoning about the wrong one.
    [[nodiscard]] Result<void> ReloadItem(DocumentSet& documents, std::size_t index);

    /// Writes the item's document, stopping neither to ask nor to check ChangedOnDisk —
    /// whether a stale write should be refused or confirmed is the caller's decision,
    /// because only the caller can ask. An item carrying a reason rather than a document
    /// succeeds having written nothing: there was never anything to save.
    ///
    /// A successful write also marks the item present, unconditionally. A document marked
    /// Missing whose file this just recreated is there again, and MarkPresent already
    /// refuses to touch Binary, TooLarge and ReadFailed.
    [[nodiscard]] Result<void> SaveDocument(DocumentSet& documents, std::size_t index);

    /// What SaveAllModified did. Deliberately not a sentence: counting what happened is
    /// policy and belongs here, wording it belongs to whoever has somewhere to say it.
    struct SaveAllReport
    {
        /// How many documents were written.
        std::size_t saved = 0;

        /// Every document skipped because its file had changed on disk, in the order the
        /// batch met them. Whole paths rather than file names — how much of a path to show
        /// is a presentation decision, and this is not the place for one.
        std::vector<std::filesystem::path> conflicted;

        /// The document a write failure stopped the batch at, and why.
        struct Failure
        {
            std::filesystem::path path;
            Error error;
        };

        /// Empty unless the batch stopped early.
        std::optional<Failure> stoppedAt;

        /// Whether every modified document ended up on disk. False for a stopped batch and
        /// false for one that ran to the end with documents skipped over a conflict: both
        /// mean some dirty document is not actually saved, which is the only thing a
        /// caller about to close something needs to know.
        [[nodiscard]] bool Complete() const noexcept
        {
            return !stoppedAt.has_value() && conflicted.empty();
        }
    };

    /// Saves every modified document, under two rules that differ on purpose.
    ///
    /// A document whose ChangedOnDisk() is true is skipped and collected, and the batch
    /// continues: one stale file among five dirty ones is no reason to leave the other
    /// four unsaved. It is not overwritten silently either — asking about each conflict in
    /// turn is the modal storm a queued question exists to avoid, so a conflict gets its
    /// Overwrite/Cancel question from a deliberate per-document save instead.
    ///
    /// A genuine write failure is different: it stops the batch on the spot, since a batch
    /// that keeps going past a real I/O error is a batch pretending nothing went wrong.
    /// The failing document stays dirty with its undo history intact, and the report names
    /// it and carries its error.
    [[nodiscard]] SaveAllReport SaveAllModified(DocumentSet& documents);
}
