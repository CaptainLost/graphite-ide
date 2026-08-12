#pragma once

#include "Core/Text/Caret.h"
#include "Core/Text/RowMap.h"
#include "Core/Text/TextDocument.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace Core
{
    /// Why an open item carries no document.
    enum class OpenFailure
    {
        None,
        Binary,
        TooLarge,
        /// The file was there when it opened and has since been deleted. The item stays:
        /// the content is in memory, so closing it would be the only way to lose
        /// something we still have.
        Missing,
        ReadFailed
    };

    /// `OpenItem::rowAimAt` when nothing is aiming at a row column. No caret offset can
    /// equal it — an offset is bounded by the document's length — which is what lets the
    /// aim be validated by a single comparison against the caret.
    inline constexpr std::size_t NoRowAim = static_cast<std::size_t>(-1);

    /// One thing the user has open. Not a document — a document *or* the reason there
    /// isn't one, because a binary file, a file past the size ceiling and a file deleted
    /// from disk are all tabs with nothing to show.
    struct OpenItem
    {
        std::filesystem::path path;
        std::optional<TextDocument> document;
        OpenFailure failure = OpenFailure::None;
        std::uint64_t size = 0;
        Caret caret;
        bool preview = false;

        /// The widest line in the document, in columns (see Columns.h), computed once
        /// when the document is loaded rather than per frame — WidestColumn walks every
        /// line and allocates a std::string for each, which is fine once but would defeat
        /// the point of an editor that only draws its visible rows if it ran every frame.
        ///
        /// Nothing in this stage edits a document, so the value never goes stale here.
        /// Whichever stage adds editing must keep this current itself: growing it when an
        /// edit makes a line wider is cheap, but shrinking it back down after an edit that
        /// removes the widest line requires rescanning the whole document, the same way
        /// loading does.
        std::size_t widestColumn = 0;

        /// Which line widestColumn was measured from. Editing any other line can only
        /// raise the value, in constant time; only editing or deleting this one requires
        /// walking the document again. Without it, the value could only ever grow, and a
        /// document whose longest line was deleted would keep offering a horizontal
        /// scroll range with nothing in it.
        std::size_t widestLine = 0;

        /// Screen rows to lines. Rebuilt by RefreshDerived, kept current line by line by
        /// UpdateDerived, and rebuilt again by the widget when the width it wraps to
        /// changes — the item cannot know that width, only the widget can.
        RowMap rows;

        /// The width `rows` was last rebuilt for by the widget, in columns, so the rebuild
        /// is skipped on the frames the width did not change — which is nearly all of
        /// them. Zero rather than RowMap::kNoWrap: zero is not a width the widget can ask
        /// for, so the first frame of a newly opened document always rebuilds and never
        /// mistakes the unwrapped map RefreshDerived left behind for one already built to
        /// the window.
        std::size_t wrappedTo = 0;

        /// The column a run of vertical moves is aiming at, measured within the screen row
        /// rather than within the line, and the caret offset it was measured at. Owned by
        /// the widget: `rows` above is the only thing that can turn one into the other, and
        /// only the widget knows the width that map was built for.
        ///
        /// Not `Caret::desiredColumn`. That field is Core's, and Core fills it with a
        /// column within the *line* — `Move`'s non-vertical branches and `PlaceAt` are its
        /// only two writers; every edit, undo, redo and the three selection helpers leave
        /// it at `NoDesiredColumn`. The two columns are the same number only while nothing
        /// wraps, so vertical motion over rows needs its own. Repairing Core's field behind
        /// its back instead would have to happen in the right order relative to every other
        /// thing that can move the caret in one frame, and would still miss a move that
        /// rewrote the column without moving the offset.
        ///
        /// Validated by offset rather than by a flag: the aim is used only when the caret
        /// is still exactly where it was computed for, so a click, an arrow key, an edit or
        /// a rebuild invalidates it without having to know it exists. `rowAim` is
        /// meaningless unless `rowAimAt` is a real offset.
        std::size_t rowAim = 0;
        std::size_t rowAimAt = NoRowAim;

        /// One indentation depth per line, in columns (see Indent.h), computed once when
        /// the document is loaded rather than per frame, the same way and for the same
        /// reason as widestColumn above — IndentDepths walks every line and allocates a
        /// std::string for each.
        ///
        /// Nothing in this stage edits a document, so the value never goes stale here.
        /// Whichever stage adds editing must keep this current itself: an edit can change
        /// the depth of the line it lands on, ripple into the blank lines around it
        /// through the resolution rule in Indent.h, and change nothing else — recomputing
        /// only the affected run rather than the whole document is possible but is not
        /// done here.
        std::vector<std::size_t> indentDepths;

        /// A specific message from whatever failed underneath — ProbeFile's own text for
        /// a directory, a locked file, or one that could not be measured. Empty for the
        /// failures classified from a successful probe, where the enumerator alone
        /// already says everything there is to say. FailureMessage prefers this when
        /// it is not empty, so a specific reason is never thrown away in favour of a
        /// vaguer one reconstructed from the enumerator.
        std::string detail;
    };

    /// What LoadFromFile did.
    enum class ItemLoad
    {
        /// The item now carries a document holding the file's content.
        Loaded,

        /// Past LargeFileThreshold and not confirmed. Nothing was read and nothing about
        /// the item changed but its size, which is the number the asking needs. Ask, then
        /// call again with `confirmed`.
        NeedsConfirmation,

        /// Nothing to show, and the item now says why: Binary, TooLarge, Missing or
        /// ReadFailed, with `detail` carrying the message where there was one.
        Failed
    };

    /// Fills an item from the file its path names, deciding as Open does — probe first,
    /// then load, refuse or ask.
    ///
    /// Takes an item rather than a set and an index because it needs nothing else: it is
    /// the one place a file becomes an item's content, whether the item is being created
    /// (DocumentSet::Add) or was created without content and its file has since appeared
    /// (FileSyncOutcome::NeedsLoad). Two loaders would be two answers to "what may be
    /// opened" waiting to differ.
    ///
    /// Whatever the item last failed with is cleared once the file has actually been
    /// looked at, so an item that opened Missing and now loads stops claiming to be
    /// missing. The exception is NeedsConfirmation, which decides nothing and so changes
    /// nothing.
    [[nodiscard]] ItemLoad LoadFromFile(OpenItem& item, bool confirmed);

    enum class OpenOutcome
    {
        /// A new item was added.
        Opened,
        /// The file was already open; it was made active instead.
        Activated,
        /// Past the size at which the user is asked. Nothing was added — call
        /// OpenConfirmed to go ahead.
        NeedsConfirmation,
        /// An item was added carrying a reason rather than a document.
        Failed
    };

    /// What the user should be told about an item with no document. English, because
    /// every string in this codebase is.
    [[nodiscard]] std::string FailureMessage(const OpenItem& item);

    /// The set of open documents. Knows nothing about ImGui, which is what makes the
    /// rules here testable — the Graphite project has no tests at all.
    class DocumentSet
    {
    public:
        /// Opens a file, or activates it when it is already open.
        ///
        /// `permanent` distinguishes a double click from a single one: a single click
        /// opens a preview, of which there is at most one at a time.
        OpenOutcome Open(const std::filesystem::path& path, bool permanent);

        /// Opens a file the caller has confirmed despite its size.
        OpenOutcome OpenConfirmed(const std::filesystem::path& path, bool permanent);

        void Close(std::size_t index);
        void CloseAll();

        [[nodiscard]] std::optional<std::size_t> Find(const std::filesystem::path& path) const;
        [[nodiscard]] std::size_t GetCount() const noexcept { return m_Items.size(); }

        [[nodiscard]] OpenItem& At(std::size_t index) { return m_Items[index]; }
        [[nodiscard]] const OpenItem& At(std::size_t index) const { return m_Items[index]; }

        void Activate(std::size_t index);
        [[nodiscard]] std::optional<std::size_t> GetActive() const noexcept { return m_Active; }

        /// Turns a preview into a permanent item, freeing the preview slot. A double
        /// click does this, and in stage 3 so will the first edit.
        void Promote(std::size_t index);

        [[nodiscard]] std::optional<std::size_t> GetPreview() const;

        /// What the tab should say.
        ///
        /// Usually the file name. When another open item shares that name, both gain
        /// their parent directory — two indistinguishable `main.cpp` tabs is an everyday
        /// situation in a repository with more than one project in it.
        ///
        /// An item marked missing gains "(deleted)". Its content is still in memory and
        /// still drawn, so without this nothing on screen would say the file behind it
        /// has gone.
        [[nodiscard]] std::string DisplayTitle(std::size_t index) const;

        /// Rewrites every path below `from` to sit below `to`, after the workspace root
        /// was renamed or moved on disk.
        void RewritePaths(const std::filesystem::path& from, const std::filesystem::path& to);

        /// The file went away while it was open. The item stays and keeps its content —
        /// closing it would be the only way to lose something still in memory.
        void MarkMissing(const std::filesystem::path& path);

        /// The file that was marked missing has come back. Clears the missing flag only
        /// — if the item failed for another reason (Binary, TooLarge, ReadFailed), this
        /// does nothing, because those failures carry no document and clearing them would
        /// claim a tab holds content that was never loaded.
        void MarkPresent(const std::filesystem::path& path);

    private:
        OpenOutcome Add(const std::filesystem::path& path, bool permanent, bool confirmed);

        std::vector<OpenItem> m_Items;
        std::optional<std::size_t> m_Active;
    };
}
