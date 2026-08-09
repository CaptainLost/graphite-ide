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
}
