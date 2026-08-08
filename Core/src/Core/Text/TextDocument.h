#pragma once

#include "Core/Base/Result.h"
#include "Core/Text/Encoding.h"
#include "Core/Text/TextBuffer.h"

#include <filesystem>
#include <string>

namespace core::text
{
    enum class LineEnding
    {
        Lf,
        Crlf
    };

    /// An open text file: its content, and everything tying that content to the file it
    /// came from.
    ///
    /// Knows nothing about ImGui, which is what makes it testable.
    class TextDocument
    {
    public:
        /// Builds a document from bytes already in hand.
        ///
        /// This is where every decision about the file's form is made: the BOM is
        /// recognised and stripped, UTF-8 is validated, and CRLF is normalised to LF
        /// while the dominant style is remembered. Fails only for an encoding that
        /// cannot be decoded at all, which today means UTF-16.
        [[nodiscard]] static Result<TextDocument> FromContent(std::string bytes,
                                                              std::filesystem::path path);

        /// Reads the file and hands it to FromContent.
        [[nodiscard]] static Result<TextDocument> Load(const std::filesystem::path& path);

        [[nodiscard]] TextBuffer& Buffer() noexcept { return m_buffer; }
        [[nodiscard]] const TextBuffer& Buffer() const noexcept { return m_buffer; }

        [[nodiscard]] const std::filesystem::path& Path() const noexcept { return m_path; }

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
        /// there was one, and the remembered line ending throughout.
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
        std::size_t m_savedRevision = 0;
    };
}
