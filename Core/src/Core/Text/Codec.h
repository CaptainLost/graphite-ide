#pragma once

#include "Core/Base/Result.h"
#include "Core/Text/Encoding.h"

#include <string>
#include <string_view>

namespace core::text
{
    /// Turns bytes from disk into the UTF-8 the rest of the editor works in.
    ///
    /// Utf8, Utf8Bom and Unknown pass through untouched — the byte order mark is the
    /// caller's business, and Unknown means bytes that failed validation and are kept
    /// verbatim. Only the UTF-16 arms transform anything.
    ///
    /// Fails when UTF-16 input is malformed: an odd byte count, or a surrogate half
    /// without its partner. Such a file cannot be both shown and preserved, because UTF-8
    /// has no way to write a lone half, so it is refused with a message naming the cause.
    [[nodiscard]] Result<std::string> Decode(std::string_view bytes, Encoding encoding);

    /// The inverse, for writing back.
    ///
    /// Returns a plain string rather than a Result because it cannot fail: UTF-8 and
    /// UTF-16 cover the same characters, so a buffer holding valid UTF-8 always encodes.
    /// That changes when single-byte codepages arrive and a character may have no
    /// representation at all.
    ///
    /// Utf8, Utf8Bom and Unknown return the input unchanged. Passing Unknown through is
    /// what keeps a read-only file whose bytes failed UTF-8 validation byte-identical
    /// when it is saved.
    [[nodiscard]] std::string Encode(std::string_view utf8, Encoding encoding);

    /// The encoding of a file, from its byte order mark or — failing that — from the
    /// shape of its bytes.
    ///
    /// A mark settles it outright. Without one, UTF-16 is still recognisable: 'A' is
    /// U+0041, so little-endian writes 41 00 and big-endian 00 41, which puts the NUL at
    /// an odd or an even position respectively. The parity of the NUL bytes therefore
    /// answers both "is this UTF-16" and "which byte order" in one pass.
    ///
    /// Three conditions, all required, because a file of small 16-bit integers has the
    /// same byte pattern as UTF-16 text and must not be mistaken for it — once saving
    /// exists, writing such a file back would destroy it:
    ///
    ///  - at least 90% of the NUL bytes sit at one parity. Not 100%: U+0100 is 00 01 in
    ///    little-endian, putting a NUL at the other parity, and demanding purity would
    ///    reject legitimate files;
    ///  - at least two NUL bytes, so a single coincidence does not decide;
    ///  - the bytes decode, in the implied order, to something shaped like text — no
    ///    unpaired surrogates, and no control characters besides tab, newline and
    ///    carriage return.
    ///
    /// Anything short of all three gives Utf8, which leaves the caller on whatever path
    /// it was already on. When uncertain, do not claim it is text.
    ///
    /// Tolerates input cut mid-unit, since callers pass an 8 KB sample as readily as a
    /// whole file.
    [[nodiscard]] Encoding SniffEncoding(std::string_view bytes);

    /// Whether Decode/Encode actually transform content for this encoding, as opposed to
    /// passing it through unchanged. The single source of truth for which arms of
    /// Decode's switch do something, so a caller deciding whether content needs UTF-8
    /// validation (only content Decode did not already guarantee is valid UTF-8 needs
    /// it) never has to duplicate that switch and risk it drifting out of step —
    /// especially once single-byte codepages join the encodings that transform.
    [[nodiscard]] bool TransformsContent(Encoding encoding);
}
