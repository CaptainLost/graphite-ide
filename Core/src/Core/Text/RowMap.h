#pragma once

#include <cstddef>
#include <vector>

namespace core::text
{
    class TextBuffer;

    /// Which line a screen row belongs to, and which of that line's rows it is.
    struct RowRef
    {
        std::size_t line = 0;
        std::size_t subRow = 0;
    };

    /// Maps screen rows to lines of the file and back.
    ///
    /// The editor is built on "one screen row is one line", which wrapping removes: a
    /// line occupies as many rows as it has breaks plus one. This class is the only place
    /// that knows the difference, so drawing, hit-testing and caret movement can all ask
    /// rather than each deriving it.
    ///
    /// A line may occupy **zero** rows. Nothing here produces that yet — wrapping never
    /// gives fewer than one — but every lookup handles it, because that is exactly what a
    /// folded line will be in stage 5. Building it in now costs a comparison; retrofitting
    /// it later would mean revisiting every caller again.
    ///
    /// Row lookup runs on a Fenwick tree over the per-line counts rather than a cumulative
    /// array. A cumulative array answers lookups just as fast, but editing one line
    /// rewrites its whole tail — a million operations per keystroke in a large file
    /// against twenty here.
    class RowMap
    {
    public:
        /// The width meaning "do not wrap at all", which is not the same as a width of
        /// zero: WrapColumns treats zero as one column and would wrap every character onto
        /// its own row. Rebuild skips measuring entirely when it sees this, giving each
        /// line one row and no breaks — which is exactly the unwrapped editor, so the
        /// widget renders through the same path whether wrapping is on or off.
        static constexpr std::size_t kNoWrap = static_cast<std::size_t>(-1);

        /// Walks the whole document. Called when the width, the font size or the tab width
        /// changes, when wrapping is switched on, and when a document is loaded or
        /// reloaded — not per frame.
        void Rebuild(const TextBuffer& buffer, std::size_t columnsPerRow, std::size_t tabWidth);

        /// Re-measures one line after an edit to it. The line count must not have changed;
        /// an edit that adds or removes lines splices with InsertLines/RemoveLines below.
        void UpdateLine(const TextBuffer& buffer, std::size_t line);

        /// Splices `count` new lines in at `at` and measures them, after an edit grew the
        /// document.
        ///
        /// The alternative is a full Rebuild, which walks every line of the document and
        /// allocates a std::string for each — a million allocations per Enter in a large
        /// file, on the path that exists precisely to avoid full walks. This re-measures
        /// only the lines that are new; the Fenwick tree is rebuilt from the counts, which
        /// is one pass of integer arithmetic over a vector and touches no text at all.
        void InsertLines(const TextBuffer& buffer, std::size_t at, std::size_t count);

        /// Drops `count` lines from `at`, after an edit shrank the document. Nothing is
        /// measured: the surviving lines keep the counts they already had.
        void RemoveLines(std::size_t at, std::size_t count);

        /// Forces a line's row count directly, bypassing measurement. The hook stage 5
        /// folds with; nothing in wrapping calls it.
        void SetRowsOf(std::size_t line, std::size_t rows);

        [[nodiscard]] std::size_t TotalRows() const noexcept { return m_totalRows; }
        [[nodiscard]] std::size_t LineCount() const noexcept { return m_rowsPerLine.size(); }

        /// The width the map was last built for, kNoWrap when it is not wrapping.
        ///
        /// Exists so a rebuild forced by something other than the width — an edit that
        /// added or removed lines, an undo — can be made at the width already in force.
        /// Only the widget knows that width, and it is not on screen when the edit
        /// happens, so without this the map would silently fall back to unwrapped.
        [[nodiscard]] std::size_t ColumnsPerRow() const noexcept { return m_columnsPerRow; }

        /// The line at `row`, and which of its rows that is. A row past the end resolves
        /// to the last line, the same way the widget already clamps a click below the
        /// document.
        [[nodiscard]] RowRef RowAt(std::size_t row) const;

        /// The first row of `line`. For a line of zero rows this is where it *would*
        /// start, which is also the first row of the next visible line.
        [[nodiscard]] std::size_t FirstRowOf(std::size_t line) const;

        [[nodiscard]] std::size_t RowsOf(std::size_t line) const;

        /// The break columns of `line`, as WrapColumns returned them. Empty for a line
        /// that fits in one row.
        [[nodiscard]] const std::vector<std::size_t>& BreaksOf(std::size_t line) const;

    private:
        void SetCount(std::size_t line, std::size_t rows);
        /// Rebuilds the Fenwick tree and the total from m_rowsPerLine, which the splices
        /// above shift wholesale — every node past the splice point covers a different set
        /// of lines afterwards, so there is nothing to patch incrementally.
        void RebuildTree();
        [[nodiscard]] std::size_t PrefixSum(std::size_t line) const;

        std::vector<std::size_t> m_rowsPerLine;
        std::vector<std::vector<std::size_t>> m_breaks;
        std::vector<std::size_t> m_tree;   // Fenwick, one-based internally
        std::size_t m_totalRows = 0;
        /// kNoWrap rather than zero, so a map that has never been built reads as "not
        /// wrapping" — zero means one column, which would wrap every character onto its
        /// own row the moment anything rebuilt at the stored width.
        std::size_t m_columnsPerRow = kNoWrap;
        std::size_t m_tabWidth = 0;
    };
}
