#include "corepch.h"

#include "Core/Text/Utf8.h"

#include "Core/Base/Assert.h"

namespace Core {

	namespace {
		constexpr char32_t kLastCodePoint = 0x10FFFF;
		constexpr char32_t kSurrogateFirst = 0xD800;
		constexpr char32_t kSurrogateLast = 0xDFFF;

		constexpr std::uint32_t kFirstTwoByteCodePoint = 0x80u;
		constexpr std::uint32_t kFirstThreeByteCodePoint = 0x800u;
		constexpr std::uint32_t kFirstFourByteCodePoint = 0x10000u;

		constexpr std::uint32_t kTwoByteLeadMarker = 0xC0u;
		constexpr std::uint32_t kThreeByteLeadMarker = 0xE0u;
		constexpr std::uint32_t kFourByteLeadMarker = 0xF0u;
		constexpr std::uint32_t kContinuationMarker = 0x80u;
		constexpr std::uint32_t kContinuationPayloadMask = 0x3Fu;
		constexpr int kBitsPerContinuation = 6;

		constexpr unsigned int kFirstNonAsciiByte = 0x80u;
		constexpr unsigned int kFirstTwoByteLead = 0xC2u;
		constexpr unsigned int kLastTwoByteLead = 0xDFu;
		constexpr unsigned int kFirstThreeByteLead = 0xE0u;
		constexpr unsigned int kSurrogateLead = 0xEDu;
		constexpr unsigned int kLastThreeByteLead = 0xEFu;
		constexpr unsigned int kFirstFourByteLead = 0xF0u;
		constexpr unsigned int kLastFourByteLead = 0xF4u;

		constexpr unsigned int kTwoByteLeadPayloadMask = 0x1Fu;
		constexpr unsigned int kThreeByteLeadPayloadMask = 0x0Fu;
		constexpr unsigned int kFourByteLeadPayloadMask = 0x07u;

		constexpr std::uint8_t kLowestSecondByteAfterFirstThreeByteLead = 0xA0;
		constexpr std::uint8_t kHighestSecondByteAfterSurrogateLead = 0x9F;
		constexpr std::uint8_t kLowestSecondByteAfterFirstFourByteLead = 0x90;
		constexpr std::uint8_t kHighestSecondByteAfterLastFourByteLead = 0x8F;

		struct LeadByte
		{
			std::uint8_t ContinuationCount = 0;
			bool CanStartCharacter = false;
			char32_t PayloadBits = 0;
			std::uint8_t LowestFirstContinuation = Utf8Decoder::kLowestContinuationByte;
			std::uint8_t HighestFirstContinuation = Utf8Decoder::kHighestContinuationByte;
		};

		// Unicode's table of well-formed byte sequences, read by its first byte. Narrowing the second
		// byte's range is what rules out overlongs (after E0 and F0), surrogates (after ED) and values
		// above U+10FFFF (after F4); C0, C1 and F5 to FF can never lead.
		LeadByte ReadLeadByte(unsigned char byte)
		{
			if (byte < kFirstNonAsciiByte)
			{
				return LeadByte{.ContinuationCount = 0, .CanStartCharacter = true, .PayloadBits = byte};
			}

			if (byte >= kFirstTwoByteLead && byte <= kLastTwoByteLead)
			{
				return LeadByte{.ContinuationCount = 1,
								.CanStartCharacter = true,
								.PayloadBits = static_cast<char32_t>(byte & kTwoByteLeadPayloadMask)};
			}

			if (byte >= kFirstThreeByteLead && byte <= kLastThreeByteLead)
			{
				const bool couldEncodeOverlong = byte == kFirstThreeByteLead;
				const bool couldEncodeSurrogate = byte == kSurrogateLead;

				const std::uint8_t lowestFirstContinuation = couldEncodeOverlong ? kLowestSecondByteAfterFirstThreeByteLead : Utf8Decoder::kLowestContinuationByte;
				const std::uint8_t highestFirstContinuation = couldEncodeSurrogate ? kHighestSecondByteAfterSurrogateLead : Utf8Decoder::kHighestContinuationByte;

				return LeadByte{.ContinuationCount = 2,
								.CanStartCharacter = true,
								.PayloadBits = static_cast<char32_t>(byte & kThreeByteLeadPayloadMask),
								.LowestFirstContinuation = lowestFirstContinuation,
								.HighestFirstContinuation = highestFirstContinuation};
			}

			if (byte >= kFirstFourByteLead && byte <= kLastFourByteLead)
			{
				const bool couldEncodeOverlong = byte == kFirstFourByteLead;
				const bool couldExceedLastCodePoint = byte == kLastFourByteLead;

				const std::uint8_t lowestFirstContinuation = couldEncodeOverlong ? kLowestSecondByteAfterFirstFourByteLead : Utf8Decoder::kLowestContinuationByte;
				const std::uint8_t highestFirstContinuation = couldExceedLastCodePoint ? kHighestSecondByteAfterLastFourByteLead : Utf8Decoder::kHighestContinuationByte;

				return LeadByte{.ContinuationCount = 3,
								.CanStartCharacter = true,
								.PayloadBits = static_cast<char32_t>(byte & kFourByteLeadPayloadMask),
								.LowestFirstContinuation = lowestFirstContinuation,
								.HighestFirstContinuation = highestFirstContinuation};
			}

			return LeadByte{.CanStartCharacter = false};
		}
	}

	std::optional<char32_t> DecodeUtf8(std::string_view bytes, std::size_t& offset)
	{
		CORE_ASSERT(offset < bytes.size(), "DecodeUtf8 needs a byte to read at the offset");

		Utf8Decoder decoder;

		while (offset < bytes.size())
		{
			const Utf8Step step = decoder.Feed(bytes[offset]);

			switch (step.Status)
			{
			case Utf8Status::Pending:
				++offset;
				break;
			case Utf8Status::Decoded:
				++offset;
				return step.CodePoint;
			case Utf8Status::Invalid:
				++offset;
				return std::nullopt;
			case Utf8Status::Interrupted:
				return std::nullopt;
			}
		}

		return std::nullopt;
	}

	void AppendUtf8(std::string& text, char32_t codePoint)
	{
		const bool isSurrogate = codePoint >= kSurrogateFirst && codePoint <= kSurrogateLast;
		const bool isNotACharacter = codePoint > kLastCodePoint || isSurrogate;

		if (isNotACharacter)
		{
			codePoint = kReplacementCharacter;
		}

		const auto scalarValue = static_cast<std::uint32_t>(codePoint);

		if (scalarValue < kFirstTwoByteCodePoint)
		{
			text.push_back(static_cast<char>(scalarValue));
		}
		else if (scalarValue < kFirstThreeByteCodePoint)
		{
			text.push_back(static_cast<char>(kTwoByteLeadMarker | (scalarValue >> kBitsPerContinuation)));
			text.push_back(static_cast<char>(kContinuationMarker | (scalarValue & kContinuationPayloadMask)));
		}
		else if (scalarValue < kFirstFourByteCodePoint)
		{
			text.push_back(static_cast<char>(kThreeByteLeadMarker | (scalarValue >> (2 * kBitsPerContinuation))));
			text.push_back(static_cast<char>(kContinuationMarker | ((scalarValue >> kBitsPerContinuation) & kContinuationPayloadMask)));
			text.push_back(static_cast<char>(kContinuationMarker | (scalarValue & kContinuationPayloadMask)));
		}
		else
		{
			text.push_back(static_cast<char>(kFourByteLeadMarker | (scalarValue >> (3 * kBitsPerContinuation))));
			text.push_back(static_cast<char>(kContinuationMarker | ((scalarValue >> (2 * kBitsPerContinuation)) & kContinuationPayloadMask)));
			text.push_back(static_cast<char>(kContinuationMarker | ((scalarValue >> kBitsPerContinuation) & kContinuationPayloadMask)));
			text.push_back(static_cast<char>(kContinuationMarker | (scalarValue & kContinuationPayloadMask)));
		}
	}

	Utf8Step Utf8Decoder::Feed(char byte)
	{
		const auto byteValue = static_cast<unsigned char>(byte);

		if (m_RemainingContinuations > 0)
		{
			const bool continuesPendingCharacter = byteValue >= m_LowestNextByte && byteValue <= m_HighestNextByte;

			if (!continuesPendingCharacter)
			{
				m_RemainingContinuations = 0;
				m_PendingValue = 0;
				return Utf8Step{.Status = Utf8Status::Interrupted, .CodePoint = 0};
			}

			m_PendingValue = (m_PendingValue << kBitsPerContinuation) | static_cast<char32_t>(byteValue & kContinuationPayloadMask);
			m_LowestNextByte = kLowestContinuationByte;
			m_HighestNextByte = kHighestContinuationByte;
			--m_RemainingContinuations;

			if (m_RemainingContinuations > 0)
			{
				return Utf8Step{.Status = Utf8Status::Pending, .CodePoint = 0};
			}

			const char32_t codePoint = m_PendingValue;
			m_PendingValue = 0;
			return Utf8Step{.Status = Utf8Status::Decoded, .CodePoint = codePoint};
		}

		const LeadByte lead = ReadLeadByte(byteValue);

		if (!lead.CanStartCharacter)
		{
			return Utf8Step{.Status = Utf8Status::Invalid, .CodePoint = 0};
		}

		if (lead.ContinuationCount == 0)
		{
			return Utf8Step{.Status = Utf8Status::Decoded, .CodePoint = lead.PayloadBits};
		}

		m_PendingValue = lead.PayloadBits;
		m_RemainingContinuations = lead.ContinuationCount;
		m_LowestNextByte = lead.LowestFirstContinuation;
		m_HighestNextByte = lead.HighestFirstContinuation;
		return Utf8Step{.Status = Utf8Status::Pending, .CodePoint = 0};
	}
}
