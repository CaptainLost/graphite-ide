#include "corepch.h"

#include "Core/Text/Encoding.h"

#include <cstdint>

namespace Core
{
    namespace
    {
        [[nodiscard]] unsigned char ByteAt(std::string_view bytes, std::size_t offset)
        {
            return static_cast<unsigned char>(bytes[offset]);
        }

        [[nodiscard]] bool IsContinuation(unsigned char byte)
        {
            return (byte & 0xC0u) == 0x80u;
        }
    }

    std::size_t BomLength(std::string_view bytes)
    {
        return BomLength(DetectEncoding(bytes));
    }

    std::size_t BomLength(Encoding encoding)
    {
        // No default: fatalwarnings turns a missing case here into a build error the
        // moment a new encoding is added, the same guarantee EncodingName relies on.
        switch (encoding)
        {
        case Encoding::Utf8Bom:
            return 3;
        case Encoding::Utf16Le:
        case Encoding::Utf16Be:
            return 2;
        case Encoding::Utf8:
        case Encoding::Unknown:
            break;
        }

        return 0;
    }

    std::string_view BomBytes(Encoding encoding)
    {
        switch (encoding)
        {
        case Encoding::Utf8Bom:
            return "\xEF\xBB\xBF";
        case Encoding::Utf16Le:
            return std::string_view{"\xFF\xFE", 2};
        case Encoding::Utf16Be:
            return std::string_view{"\xFE\xFF", 2};
        case Encoding::Utf8:
        case Encoding::Unknown:
            break;
        }

        return {};
    }

    Encoding DetectEncoding(std::string_view bytes)
    {
        if (bytes.size() >= 3 && ByteAt(bytes, 0) == 0xEFu && ByteAt(bytes, 1) == 0xBBu
            && ByteAt(bytes, 2) == 0xBFu)
            return Encoding::Utf8Bom;

        if (bytes.size() >= 2 && ByteAt(bytes, 0) == 0xFFu && ByteAt(bytes, 1) == 0xFEu)
            return Encoding::Utf16Le;

        if (bytes.size() >= 2 && ByteAt(bytes, 0) == 0xFEu && ByteAt(bytes, 1) == 0xFFu)
            return Encoding::Utf16Be;

        return Encoding::Utf8;
    }

    bool IsValidUtf8(std::string_view bytes)
    {
        std::size_t position = 0;

        while (position < bytes.size())
        {
            const unsigned char lead = ByteAt(bytes, position);

            if (lead < 0x80u)
            {
                ++position;
                continue;
            }

            std::size_t length = 0;
            std::uint32_t codepoint = 0;

            if ((lead & 0xE0u) == 0xC0u)
            {
                length = 2;
                codepoint = lead & 0x1Fu;
            }
            else if ((lead & 0xF0u) == 0xE0u)
            {
                length = 3;
                codepoint = lead & 0x0Fu;
            }
            else if ((lead & 0xF8u) == 0xF0u)
            {
                length = 4;
                codepoint = lead & 0x07u;
            }
            else
            {
                // A continuation byte with nothing leading it, or a five-byte lead.
                return false;
            }

            if (position + length > bytes.size())
                return false;

            for (std::size_t index = 1; index < length; ++index)
            {
                const unsigned char next = ByteAt(bytes, position + index);

                if (!IsContinuation(next))
                    return false;

                codepoint = (codepoint << 6) | (next & 0x3Fu);
            }

            // The shape can be right while the value is wrong. An overlong form encodes a
            // small value in more bytes than it needs, which is how '/' gets smuggled
            // past a naive path check; surrogates and values above U+10FFFF are not
            // characters at all.
            if (length == 2 && codepoint < 0x80u)
                return false;
            if (length == 3 && codepoint < 0x800u)
                return false;
            if (length == 4 && codepoint < 0x10000u)
                return false;
            if (codepoint > 0x10FFFFu)
                return false;
            if (codepoint >= 0xD800u && codepoint <= 0xDFFFu)
                return false;

            position += length;
        }

        return true;
    }

    std::size_t NextCharacter(std::string_view bytes, std::size_t offset)
    {
        if (offset >= bytes.size())
            return bytes.size();

        std::size_t next = offset + 1;

        while (next < bytes.size() && IsContinuation(ByteAt(bytes, next)))
            ++next;

        return next;
    }

    std::size_t PreviousCharacter(std::string_view bytes, std::size_t offset)
    {
        if (offset == 0 || bytes.empty())
            return 0;

        std::size_t previous = (offset > bytes.size() ? bytes.size() : offset) - 1;

        while (previous > 0 && IsContinuation(ByteAt(bytes, previous)))
            --previous;

        return previous;
    }

    std::string_view EncodingName(Encoding encoding)
    {
        switch (encoding)
        {
        case Encoding::Utf8:
            return "UTF-8";
        case Encoding::Utf8Bom:
            return "UTF-8 with BOM";
        case Encoding::Utf16Le:
            return "UTF-16 LE";
        case Encoding::Utf16Be:
            return "UTF-16 BE";
        case Encoding::Unknown:
            break;
        }

        return "Unknown";
    }
}
