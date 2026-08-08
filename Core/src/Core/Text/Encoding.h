#pragma once

#include <cstddef>
#include <string_view>

namespace core::text
{
    /// How a file's bytes encode its characters.
    ///
    /// Detection is complete; decoding is not. TextDocument loads Utf8 and Utf8Bom and
    /// refuses the rest by name, so a UTF-16 file gets an honest message instead of
    /// being called binary. Adding UTF-16 later means adding a branch there, not
    /// changing anything here.
    enum class Encoding
    {
        Utf8,
        Utf8Bom,
        Utf16Le,
        Utf16Be,
        /// Not valid UTF-8 and carrying no BOM that explains why. The bytes are kept as
        /// they came and the document opens read-only.
        Unknown
    };

    /// Length of the byte order mark starting `bytes`, or 0 when there is none.
    [[nodiscard]] std::size_t BomLength(std::string_view bytes);

    /// The encoding the leading bytes declare. Reads the BOM only, never the content, so
    /// a file with no BOM is reported as Utf8 whether or not it validates — deciding
    /// that is IsValidUtf8's job, on the whole content rather than on a truncated sample.
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
