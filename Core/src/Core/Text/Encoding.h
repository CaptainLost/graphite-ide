#pragma once

#include <cstddef>
#include <string_view>

namespace Core
{
    /// How a file's bytes encode its characters.
    ///
    /// Detection and decoding both cover every enumerator here: Codec turns each of them
    /// into UTF-8 and back, and TextDocument opens all of them rather than refusing any by
    /// name. Adding one means an arm in each switch over this enumeration — none of which
    /// carries a default, so the compiler asks for them instead of letting a new encoding
    /// fall quietly through a catch-all.
    enum class Encoding
    {
        Utf8,
        Utf8Bom,
        Utf16Le,
        Utf16Be,
        /// Not valid UTF-8 and carrying no BOM that explains why. The document opens
        /// read-only, and its bytes are kept as they came except for line-ending
        /// normalisation, which runs regardless of validity — a mixed-ending read-only
        /// file still comes back with its endings unified, not byte-identical.
        Unknown
    };

    /// Length of the byte order mark starting `bytes`, or 0 when there is none.
    [[nodiscard]] std::size_t BomLength(std::string_view bytes);

    /// Length of the byte order mark for an already-detected encoding. Lets a caller
    /// that already ran DetectEncoding reuse the result instead of scanning the bytes a
    /// second time.
    [[nodiscard]] std::size_t BomLength(Encoding encoding);

    /// The bytes of the mark this encoding is written with, or empty when it has none.
    /// The counterpart of BomLength: that says how many bytes to skip on the way in,
    /// this says which bytes to write on the way out.
    [[nodiscard]] std::string_view BomBytes(Encoding encoding);

    /// The encoding the leading bytes declare. Reads the BOM only, never the content, so
    /// a file with no BOM is reported as Utf8 whether or not it validates — deciding
    /// that is IsValidUtf8's job, on the whole content rather than on a truncated sample.
    ///
    /// Not the function to ask what encoding a file is in: an unmarked UTF-16 file comes
    /// back Utf8 from here. Codec's SniffEncoding wraps this and reads the content when
    /// the mark says nothing, and that is what a caller deciding how to read a file wants.
    [[nodiscard]] Encoding DetectEncoding(std::string_view bytes);

    /// Whether every byte belongs to a well-formed UTF-8 sequence. Rejects truncated
    /// sequences, lone continuation bytes, overlong forms, surrogate halves, and
    /// anything above U+10FFFF — all of which decode "successfully" if you only check
    /// the shape of the bytes.
    [[nodiscard]] bool IsValidUtf8(std::string_view bytes);

    /// Offset of the next character boundary at or after `offset` + 1, clamped to the
    /// end. Steps over a whole UTF-8 sequence, so a cursor never lands inside one.
    [[nodiscard]] std::size_t NextCharacter(std::string_view bytes, std::size_t offset);

    /// Offset of the character boundary before `offset`, clamped to 0.
    [[nodiscard]] std::size_t PreviousCharacter(std::string_view bytes, std::size_t offset);

    /// Name for the status bar and for error messages.
    [[nodiscard]] std::string_view EncodingName(Encoding encoding);
}
