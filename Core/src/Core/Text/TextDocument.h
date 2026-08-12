#pragma once

#include "Core/Base/Result.h"
#include "Core/Text/Encoding.h"
#include "Core/Text/TextBuffer.h"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace Core
{
    enum class LineEnding
    {
        Lf,
        Crlf
    };

    /// LF-only text rewritten with `ending`.
    ///
    /// Everything inside the editor is LF — TextBuffer says so, and TextDocument
    /// normalises on load — so LF is the only thing this ever has to read. It is what
    /// puts the file's own ending back when saving, and what the Windows clipboard needs
    /// on the way out: a Win32 EDIT control and every spreadsheet read LF-only text as a
    /// single line, so copying five lines and pasting them elsewhere would otherwise
    /// arrive as one.
    [[nodiscard]] std::string WithLineEnding(std::string_view text, LineEnding ending);

    /// An open text file: its content, and everything tying that content to the file it
    /// came from.
    ///
    /// Knows nothing about ImGui, which is what makes it testable.
    class TextDocument
    {
    public:
        /// Builds a document from bytes already in hand.
        ///
        /// This is where every decision about the file's form is made: the encoding is
        /// sniffed, the BOM is recognised and stripped, the content is decoded to UTF-8
        /// (validated instead, for encodings Decode passes through unchanged), and CRLF
        /// is normalised to LF while the dominant style is remembered.
        ///
        /// The encoding is sniffed from the first ProbeSampleSize bytes, not from all of
        /// them, because FileProbe decides whether this file may be opened at all from
        /// exactly those bytes and the two verdicts must not be able to differ.
        ///
        /// Fails for UTF-16 content that is damaged — an odd byte count, or a surrogate
        /// half with no partner — and for UTF-16 content that decodes cleanly but is not
        /// text after all. How much is demanded of "is text" depends on whether a mark was
        /// there: an unmarked file was only guessed at, from a sample, so all of it must
        /// read as text; a marked one stated its own encoding, so only a NUL character
        /// disqualifies it, that being the signature of a UTF-32 file whose mark begins
        /// with the UTF-16 LE one. Control characters in a marked file are content, and are
        /// kept.
        [[nodiscard]] static Result<TextDocument> FromContent(std::string bytes,
                                                              std::filesystem::path path);

        /// Reads the file and hands it to FromContent.
        [[nodiscard]] static Result<TextDocument> Load(const std::filesystem::path& path);

        [[nodiscard]] TextBuffer& GetBuffer() noexcept { return m_Buffer; }
        [[nodiscard]] const TextBuffer& GetBuffer() const noexcept { return m_Buffer; }

        [[nodiscard]] const std::filesystem::path& GetPath() const noexcept { return m_Path; }

        /// Follows the file to where it now lives, after the folder above it was renamed
        /// or moved on disk. The content is untouched — this only changes where saving
        /// will write, which is the whole reason it exists: two places holding one path
        /// is how a save silently lands at the old location.
        void SetPath(std::filesystem::path path) { m_Path = std::move(path); }

        /// Not named Encoding(): a member of that name would shadow the type
        /// Core::Encoding everywhere inside this class.
        [[nodiscard]] Encoding GetFileEncoding() const noexcept { return m_Encoding; }

        /// Likewise not named LineEnding().
        [[nodiscard]] LineEnding GetDocumentLineEnding() const noexcept { return m_LineEnding; }

        /// Whether the file carried both styles. The status bar says so, because saving
        /// unifies them and that should not be a surprise.
        [[nodiscard]] bool WasMixed() const noexcept { return m_WasMixed; }

        /// True when the content is not valid UTF-8. Typing into such a file would put
        /// UTF-8 bytes among bytes in some other encoding and quietly ruin it, so stage 3
        /// refuses to. The bytes are kept as they came except for line-ending
        /// normalisation, which runs regardless of validity — a mixed-ending read-only
        /// file still comes back with its endings unified, not byte-identical.
        [[nodiscard]] bool IsReadOnly() const noexcept { return m_ReadOnly; }

        /// The content exactly as it should reach disk: the BOM back at the front if
        /// there was one, the content re-encoded to m_Encoding, and the remembered line
        /// ending throughout.
        [[nodiscard]] std::string GetBytes() const;

        /// Whether the content differs from what was last saved.
        [[nodiscard]] bool IsModified() const noexcept
        {
            return m_Buffer.GetRevision() != m_SavedRevision;
        }

        /// Records the current state as the saved one. Called after a successful write,
        /// which stage 3 adds.
        void MarkSaved() noexcept { m_SavedRevision = m_Buffer.GetRevision(); }

        /// Whether the file differs from what was last read or written from here.
        /// False when the file cannot be examined at all — a share that blinked must not
        /// be reported as an edit — and false, for the same reason, when no stamp was ever
        /// taken: a document built by FromContent, or one whose last_write_time could not
        /// be read when it was loaded, has nothing to compare against, and inventing a
        /// difference out of that would be reporting an edit nobody made.
        [[nodiscard]] bool HasChangedOnDisk() const;

        /// Writes GetBytes() through Core::WriteFileAtomically, then marks the content
        /// saved and records the new stamp. Does not check HasChangedOnDisk() itself: whether
        /// a stale save should be refused or confirmed is the caller's decision, because
        /// only the caller can ask.
        [[nodiscard]] Result<void> Save();

        /// Replaces the content with the file's, as one undo step, and records the new
        /// stamp. The caret is the caller's to fix up.
        ///
        /// On failure nothing is touched: a file that has become binary or truncated
        /// leaves the document exactly as it was, because discarding what is in memory is
        /// the worst available answer to "the file went strange".
        [[nodiscard]] Result<void> ReloadFromDisk();

        /// Records the file's current state as known without reading it — what "keep my
        /// version" means after a conflict. The next Save() then overwrites without
        /// asking again.
        void AcceptDiskState();

    private:
        TextBuffer m_Buffer;
        std::filesystem::path m_Path;
        Encoding m_Encoding = Encoding::Utf8;
        LineEnding m_LineEnding = LineEnding::Lf;
        bool m_WasMixed = false;
        bool m_ReadOnly = false;
        /// Whether a byte order mark was stripped from the front of the loaded bytes.
        /// Tracked independently of m_Encoding: invalid UTF-8 following a mark still
        /// demotes m_Encoding to Unknown, but the mark itself was still there and GetBytes()
        /// must still put it back, or a read-only file's bytes would not round-trip.
        bool m_HadBom = false;
        /// Which mark GetBytes() writes back, and meaningful only where m_HadBom says there
        /// was one to write. It holds the sniffed encoding, not a mark that was found:
        /// SniffEncoding names a UTF-16 file from the shape of its bytes alone, so an
        /// unmarked one leaves this reading Utf16Le with no mark anywhere in the file —
        /// harmless, because m_HadBom is false and GetBytes() never asks. Kept apart from
        /// m_Encoding because invalid UTF-8 behind a real mark demotes m_Encoding to
        /// Unknown for read-only handling while the mark still has to go back.
        Encoding m_BomEncoding = Encoding::Utf8;
        std::size_t m_SavedRevision = 0;

        /// The file's modification time and size as they were when its content was last
        /// read or written, held together and as one optional because half a stamp is not
        /// a stamp: a default-constructed time compares unequal to every real file's, so a
        /// stamp that was never actually taken would make HasChangedOnDisk() permanently
        /// true — a clean document reloading on every watcher event and every focus
        /// regain, each reload pushing another transaction onto the undo stack, and a
        /// dirty one re-queuing its conflict question on every sweep. Nullopt says the
        /// stamp is unknown, and an unknown stamp is compared against nothing.
        struct DiskStamp
        {
            std::filesystem::file_time_type time{};
            std::uintmax_t size = 0;
        };

        /// Read BEFORE the content, never after. If the file is rewritten during our read,
        /// a stamp taken beforehand is older than the file's real state and the next
        /// comparison notices; taken afterwards, it would record a state newer than what
        /// was actually read and the change would be lost for good. A false "changed"
        /// costs one reload, a false "unchanged" costs somebody's work.
        ///
        /// It is also what keeps our own writes from looking like somebody else's: after a
        /// save this holds the file we just produced, so the watcher notification that
        /// eventually arrives finds nothing to do. No "ignore the next event" flag and no
        /// timeout — notifications coalesce and arrive late, and both of those tricks
        /// assume they do not.
        std::optional<DiskStamp> m_DiskStamp;
    };
}
