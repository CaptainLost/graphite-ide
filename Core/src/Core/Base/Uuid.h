#pragma once

#include <array>
#include <cstdint>
#include <string>

namespace Core
{
    /// A version 4 UUID: a hundred and twenty-two random bits in the layout RFC 4122
    /// describes, plus the four version bits and two variant bits that say so.
    ///
    /// Held as the sixteen bytes the document talks about rather than as text, so the
    /// version and variant are stamped where the document stamps them and can be read
    /// back against it. Formats that decorate a UUID — braces, lower case — do that
    /// themselves; nothing here knows about any of them.
    class Uuid
    {
    public:
        /// The number of bytes a UUID is, wherever that has to be spelled out.
        static constexpr std::size_t kByteCount = 16;

        /// Generated rather than asked of the operating system, which is all a version 4
        /// UUID is — so there is no platform branch here, and no empty result for a caller
        /// to check on the platforms that would not have had one.
        [[nodiscard]] static Uuid Generate();

        /// The canonical 8-4-4-4-12 form, uppercase and undecorated.
        [[nodiscard]] std::string ToString() const;

        [[nodiscard]] const std::array<std::uint8_t, kByteCount>& Bytes() const noexcept
        {
            return m_Bytes;
        }

    private:
        std::array<std::uint8_t, kByteCount> m_Bytes{};
    };
}
