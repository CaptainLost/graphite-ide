#pragma once

#include "Core/Base/Result.h"
#include "Core/Text/Encoding.h"
#include "Core/Text/TextBuffer.h"

#include <filesystem>
#include <string>
#include <string_view>

namespace core::text
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
        /// half with no partner — and for UTF-16 content that decodes cleanly but does not
        /// read as text, which is what a sample-sized verdict about a larger file can turn
        /// out to be wrong about.
        [[nodiscard]] static Result<TextDocument> FromContent(std::string bytes,
                                                              std::filesystem::path path);

        /// Reads the file and hands it to FromContent.
        [[nodiscard]] static Result<TextDocument> Load(const std::filesystem::path& path);

        [[nodiscard]] TextBuffer& Buffer() noexcept { return m_buffer; }
        [[nodiscard]] const TextBuffer& Buffer() const noexcept { return m_buffer; }

        [[nodiscard]] const std::filesystem::path& Path() const noexcept { return m_path; }

        /// Follows the file to where it now lives, after the folder above it was renamed
        /// or moved on disk. The content is untouched — this only changes where saving
        /// will write, which is the whole reason it exists: two places holding one path
        /// is how a save silently lands at the old location.
        void SetPath(std::filesystem::path path) { m_path = std::move(path); }

        /// Not named Encoding(): a member of that name would shadow the type
        /// core::text::Encoding everywhere inside this class.
        [[nodiscard]] Encoding FileEncoding() const noexcept { return m_encoding; }

        /// Likewise not named LineEnding().
        [[nodiscard]] LineEnding DocumentLineEnding() const noexcept { return m_lineEnding; }

        /// Whether the file carried both styles. The status bar says so, because saving
        /// unifies them and that should not be a surprise.
        [[nodiscard]] bool WasMixed() const noexcept { return m_wasMixed; }

        /// True when the content is not valid UTF-8. Typing into such a file would put
        /// UTF-8 bytes among bytes in some other encoding and quietly ruin it, so stage 3
        /// refuses to. The bytes are kept as they came except for line-ending
        /// normalisation, which runs regardless of validity — a mixed-ending read-only
        /// file still comes back with its endings unified, not byte-identical.
        [[nodiscard]] bool IsReadOnly() const noexcept { return m_readOnly; }

        /// The content exactly as it should reach disk: the BOM back at the front if
        /// there was one, the content re-encoded to m_encoding, and the remembered line
        /// ending throughout.
        [[nodiscard]] std::string Bytes() const;

        /// Whether the content differs from what was last saved.
        [[nodiscard]] bool IsModified() const noexcept
        {
            return m_buffer.Revision() != m_savedRevision;
        }

        /// Records the current state as the saved one. Called after a successful write,
        /// which stage 3 adds.
        void MarkSaved() noexcept { m_savedRevision = m_buffer.Revision(); }

    private:
        TextBuffer m_buffer;
        std::filesystem::path m_path;
        Encoding m_encoding = Encoding::Utf8;
        LineEnding m_lineEnding = LineEnding::Lf;
        bool m_wasMixed = false;
        bool m_readOnly = false;
        /// Whether a byte order mark was stripped from the front of the loaded bytes.
        /// Tracked independently of m_encoding: invalid UTF-8 following a mark still
        /// demotes m_encoding to Unknown, but the mark itself was still there and Bytes()
        /// must still put it back, or a read-only file's bytes would not round-trip.
        bool m_hadBom = false;
        /// Which mark Bytes() writes back, and meaningful only where m_hadBom says there
        /// was one to write. It holds the sniffed encoding, not a mark that was found:
        /// SniffEncoding names a UTF-16 file from the shape of its bytes alone, so an
        /// unmarked one leaves this reading Utf16Le with no mark anywhere in the file —
        /// harmless, because m_hadBom is false and Bytes() never asks. Kept apart from
        /// m_encoding because invalid UTF-8 behind a real mark demotes m_encoding to
        /// Unknown for read-only handling while the mark still has to go back.
        Encoding m_bomEncoding = Encoding::Utf8;
        std::size_t m_savedRevision = 0;
    };
}
