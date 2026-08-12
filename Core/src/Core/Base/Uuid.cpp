#include "corepch.h"

#include "Core/Base/Uuid.h"

#include <cstring>
#include <random>

namespace Core
{
    Uuid Uuid::Generate()
    {
        // random_device seeds and mt19937_64 generates: a conforming random_device is
        // allowed to be deterministic, and drawing every bit from it is slow where it is
        // not. thread_local because the engine is stateful and this may be called from the
        // UI thread and a worker.
        static thread_local std::mt19937_64 engine{std::random_device{}()};

        std::uniform_int_distribution<std::uint64_t> distribution;

        // Two draws fill all sixteen bytes. Which half lands where does not matter, and
        // neither does the byte order within a half: every byte is random, so there is no
        // ordering here to get wrong.
        const std::uint64_t halves[2] = {distribution(engine), distribution(engine)};

        static_assert(sizeof(halves) == kByteCount, "two 64-bit draws are one UUID");

        Uuid uuid;
        std::memcpy(uuid.m_Bytes.data(), halves, kByteCount);

        // RFC 4122 section 4.4: version in the top nibble of byte 6, variant in the top
        // two bits of byte 8. That leaves the other hundred and twenty-two bits random.
        // Neither solution format checks either field, but a value calling itself a UUID
        // should say which kind it is.
        uuid.m_Bytes[6] = static_cast<std::uint8_t>((uuid.m_Bytes[6] & 0x0F) | 0x40);
        uuid.m_Bytes[8] = static_cast<std::uint8_t>((uuid.m_Bytes[8] & 0x3F) | 0x80);

        return uuid;
    }

    std::string Uuid::ToString() const
    {
        // 8-4-4-4-12 hexadecimal digits is four, two, two, two and six bytes — the groups
        // written as what they are, rather than as offsets to compare an index against.
        constexpr std::size_t kGroupSizes[] = {4, 2, 2, 2, 6};

        std::string text;
        text.reserve(36);

        std::size_t byte = 0;

        for (const std::size_t size : kGroupSizes)
        {
            if (!text.empty())
                text += '-';

            for (std::size_t i = 0; i < size; ++i, ++byte)
                text += std::format("{:02X}", m_Bytes[byte]);
        }

        return text;
    }
}
