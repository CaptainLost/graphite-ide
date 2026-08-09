#pragma once

#include "Core/Text/Caret.h"
#include "Core/Text/Encoding.h"
#include "Core/Text/TextDocument.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace core::text
{
    /// Why an open item carries no document.
    enum class OpenFailure
    {
        None,
        Binary,
        TooLarge,
        UnsupportedEncoding,
        /// The file was there when it opened and has since been deleted. The item stays:
        /// the content is in memory, so closing it would be the only way to lose
        /// something we still have.
        Missing,
        ReadFailed
    };

    /// One thing the user has open. Not a document — a document *or* the reason there
    /// isn't one, because a binary file, a file past the size ceiling, a UTF-16 file and
    /// a file deleted from disk are all tabs with nothing to show.
    struct OpenItem
    {
        std::filesystem::path path;
        std::optional<TextDocument> document;
        OpenFailure failure = OpenFailure::None;
        std::uint64_t size = 0;
        Encoding encoding = Encoding::Utf8;
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
        [[nodiscard]] std::size_t Count() const noexcept { return m_items.size(); }

        [[nodiscard]] OpenItem& At(std::size_t index) { return m_items[index]; }
        [[nodiscard]] const OpenItem& At(std::size_t index) const { return m_items[index]; }

        void Activate(std::size_t index);
        [[nodiscard]] std::optional<std::size_t> Active() const noexcept { return m_active; }

        /// Turns a preview into a permanent item, freeing the preview slot. A double
        /// click does this, and in stage 3 so will the first edit.
        void Promote(std::size_t index);

        [[nodiscard]] std::optional<std::size_t> Preview() const;

        /// What the tab should say.
        ///
        /// Usually the file name. When another open item shares that name, both gain
        /// their parent directory — two indistinguishable `main.cpp` tabs is an everyday
        /// situation in a repository with more than one project in it.
        [[nodiscard]] std::string DisplayTitle(std::size_t index) const;

        /// Rewrites every path below `from` to sit below `to`, after the workspace root
        /// was renamed or moved on disk.
        void RewritePaths(const std::filesystem::path& from, const std::filesystem::path& to);

        /// The file went away while it was open. The item stays and keeps its content —
        /// closing it would be the only way to lose something still in memory.
        void MarkMissing(const std::filesystem::path& path);

    private:
        OpenOutcome Add(const std::filesystem::path& path, bool permanent, bool confirmed);

        std::vector<OpenItem> m_items;
        std::optional<std::size_t> m_active;
    };
}
