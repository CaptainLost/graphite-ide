#include "corepch.h"

#include "Core/Text/Utf8.h"

namespace Core {

	namespace {
		constexpr char32_t kLastCodePoint = 0x10FFFF;
		constexpr char32_t kSurrogateFirst = 0xD800;
		constexpr char32_t kSurrogateLast = 0xDFFF;

		struct Lead
		{
			// Continuation bytes still to come; 0 for ASCII and for a byte that cannot lead.
			std::uint8_t Continuations = 0;
			bool Valid = false;
			char32_t Bits = 0;
			// The range the first continuation byte must fall in.
			std::uint8_t Lower = 0x80;
			std::uint8_t Upper = 0xBF;
		};

		// Unicode's table of well-formed byte sequences, read by its first byte. Narrowing
		// the second byte's range is what rules out overlongs (E0, F0), surrogates (ED) and
		// values above U+10FFFF (F4); C0, C1 and F5-FF can never lead.
		Lead ReadLead(unsigned char byte)
		{
			if (byte < 0x80u)
			{
				return {0, true, byte};
			}

			if (byte >= 0xC2u && byte <= 0xDFu)
			{
				return {1, true, static_cast<char32_t>(byte & 0x1Fu)};
			}

			if (byte >= 0xE0u && byte <= 0xEFu)
			{
				const std::uint8_t lower = byte == 0xE0u ? 0xA0 : 0x80;
				const std::uint8_t upper = byte == 0xEDu ? 0x9F : 0xBF;
				return {2, true, static_cast<char32_t>(byte & 0x0Fu), lower, upper};
			}

			if (byte >= 0xF0u && byte <= 0xF4u)
			{
				const std::uint8_t lower = byte == 0xF0u ? 0x90 : 0x80;
				const std::uint8_t upper = byte == 0xF4u ? 0x8F : 0xBF;
				return {3, true, static_cast<char32_t>(byte & 0x07u), lower, upper};
			}

			return {};
		}
	}

	std::optional<char32_t> DecodeUtf8(std::string_view bytes, std::size_t& offset)
	{
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
				// The byte that broke the sequence starts the next read.
				return std::nullopt;
			}
		}

		// The input ended inside a character.
		return std::nullopt;
	}

	void AppendUtf8(std::string& out, char32_t codePoint)
	{
		if (codePoint > kLastCodePoint || (codePoint >= kSurrogateFirst && codePoint <= kSurrogateLast))
		{
			codePoint = kReplacementCharacter;
		}

		const auto value = static_cast<std::uint32_t>(codePoint);

		if (value < 0x80u)
		{
			out.push_back(static_cast<char>(value));
		}
		else if (value < 0x800u)
		{
			out.push_back(static_cast<char>(0xC0u | (value >> 6)));
			out.push_back(static_cast<char>(0x80u | (value & 0x3Fu)));
		}
		else if (value < 0x10000u)
		{
			out.push_back(static_cast<char>(0xE0u | (value >> 12)));
			out.push_back(static_cast<char>(0x80u | ((value >> 6) & 0x3Fu)));
			out.push_back(static_cast<char>(0x80u | (value & 0x3Fu)));
		}
		else
		{
			out.push_back(static_cast<char>(0xF0u | (value >> 18)));
			out.push_back(static_cast<char>(0x80u | ((value >> 12) & 0x3Fu)));
			out.push_back(static_cast<char>(0x80u | ((value >> 6) & 0x3Fu)));
			out.push_back(static_cast<char>(0x80u | (value & 0x3Fu)));
		}
	}

	Utf8Step Utf8Decoder::Feed(char byte)
	{
		const auto value = static_cast<unsigned char>(byte);

		if (m_Remaining > 0)
		{
			if (value < m_Lower || value > m_Upper)
			{
				m_Remaining = 0;
				m_Value = 0;
				return {Utf8Status::Interrupted, 0};
			}

			m_Value = (m_Value << 6) | static_cast<char32_t>(value & 0x3Fu);
			m_Lower = 0x80;
			m_Upper = 0xBF;
			--m_Remaining;

			if (m_Remaining > 0)
			{
				return {Utf8Status::Pending, 0};
			}

			const char32_t codePoint = m_Value;
			m_Value = 0;
			return {Utf8Status::Decoded, codePoint};
		}

		const Lead lead = ReadLead(value);

		if (!lead.Valid)
		{
			return {Utf8Status::Invalid, 0};
		}

		if (lead.Continuations == 0)
		{
			return {Utf8Status::Decoded, lead.Bits};
		}

		m_Value = lead.Bits;
		m_Remaining = lead.Continuations;
		m_Lower = lead.Lower;
		m_Upper = lead.Upper;
		return {Utf8Status::Pending, 0};
	}
}
