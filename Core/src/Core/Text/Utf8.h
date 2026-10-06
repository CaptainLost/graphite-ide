#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace Core {

	inline constexpr char32_t kReplacementCharacter = 0xFFFD;

	std::optional<char32_t> DecodeUtf8(std::string_view bytes, std::size_t& offset);

	void AppendUtf8(std::string& text, char32_t codePoint);

	enum class Utf8Status : std::uint8_t
	{
		Pending,
		Decoded,
		Invalid,
		// Unlike Invalid, the byte was not consumed: it broke the pending character, which was
		// dropped, and the caller must feed the byte again as the start of the next character.
		Interrupted
	};

	struct Utf8Step
	{
		Utf8Status Status = Utf8Status::Pending;
		char32_t CodePoint = 0;
	};

	class Utf8Decoder
	{
	public:
		static constexpr std::uint8_t kLowestContinuationByte = 0x80;
		static constexpr std::uint8_t kHighestContinuationByte = 0xBF;

		Utf8Step Feed(char byte);

		bool IsPending() const { return m_RemainingContinuations > 0; }

	private:
		char32_t m_PendingValue = 0;
		std::uint8_t m_RemainingContinuations = 0;
		std::uint8_t m_LowestNextByte = kLowestContinuationByte;
		std::uint8_t m_HighestNextByte = kHighestContinuationByte;
	};
}
