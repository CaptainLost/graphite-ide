#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace Core {

	// U+FFFD, what a caller shows in place of a sequence that is not UTF-8.
	inline constexpr char32_t kReplacementCharacter = 0xFFFD;

	// Strict UTF-8, the one set of rules every reader in Core and Graphite applies: an
	// overlong form, an encoded surrogate, a value above U+10FFFF, a truncated sequence and a
	// continuation byte with no lead are all invalid. What a caller does with an invalid
	// sequence — replace it, skip it, refuse the whole input — stays the caller's choice.

	// Decodes the character starting at offset and moves offset past what was read. On
	// invalid input it returns nothing and moves offset past the longest prefix that could
	// still have begun a valid character (at least one byte), so the byte that broke a
	// sequence is read again as the start of the next one. Precondition: offset < bytes.size().
	std::optional<char32_t> DecodeUtf8(std::string_view bytes, std::size_t& offset);

	// Appends the UTF-8 form of codePoint. A surrogate or a value above U+10FFFF is not a
	// character and is written as U+FFFD, so the output is always valid UTF-8.
	void AppendUtf8(std::string& out, char32_t codePoint);

	enum class Utf8Status : std::uint8_t
	{
		// The byte was taken into a character that is not finished yet.
		Pending,
		// The byte finished a character; Utf8Step::CodePoint holds it.
		Decoded,
		// The byte cannot begin a character and was consumed.
		Invalid,
		// The byte cannot continue the pending character: that character is broken and was
		// dropped, and the byte itself was not consumed and must be fed again.
		Interrupted
	};

	struct Utf8Step
	{
		Utf8Status Status = Utf8Status::Pending;
		char32_t CodePoint = 0;
	};

	// The same rules as DecodeUtf8, for a stream that arrives a byte at a time and may be
	// cut anywhere: a character split across two reads is held until it is complete.
	class Utf8Decoder
	{
	public:
		Utf8Step Feed(char byte);

		bool IsPending() const { return m_Remaining > 0; }

	private:
		char32_t m_Value = 0;
		std::uint8_t m_Remaining = 0;
		// The range the next continuation byte must fall in. Only the first continuation is
		// narrower than 0x80-0xBF: that is where overlongs, surrogates and values above
		// U+10FFFF are told apart.
		std::uint8_t m_Lower = 0x80;
		std::uint8_t m_Upper = 0xBF;
	};
}
