#pragma once

#include <cstddef>
#include <string_view>
#include <vector>

namespace Core {

	enum class Encoding
	{
		Utf8,
		Utf8Bom,
		Utf16Le,
		Utf16Be,
		Unknown
	};

	std::size_t LeadingByteOrderMarkLength(std::string_view bytes);

	std::size_t ByteOrderMarkLength(Encoding encoding);

	std::string_view ByteOrderMarkBytes(Encoding encoding);

	std::string_view WithoutUtf8ByteOrderMark(std::string_view text);

	Encoding DetectEncoding(std::string_view bytes);

	bool IsValidUtf8(std::string_view bytes);

	// Cast before masking: MSVC's char is signed, so every byte from 0x80 up arrives negative.
	constexpr bool IsUtf8ContinuationByte(char byte)
	{
		constexpr unsigned int kTopTwoBitsMask = 0xC0u;
		constexpr unsigned int kContinuationPattern = 0x80u;

		return (static_cast<unsigned char>(byte) & kTopTwoBitsMask) == kContinuationPattern;
	}

	// Lenient on purpose: anything that cannot lead a sequence counts as one byte, so a walk over
	// malformed text still advances. Bounding the walk at the end of the text is the caller's job.
	constexpr std::size_t Utf8SequenceLength(char leadByte)
	{
		constexpr unsigned int kFirstNonAsciiByte = 0x80u;
		constexpr unsigned int kTopThreeBitsMask = 0xE0u;
		constexpr unsigned int kTwoByteLeadPattern = 0xC0u;
		constexpr unsigned int kTopFourBitsMask = 0xF0u;
		constexpr unsigned int kThreeByteLeadPattern = 0xE0u;
		constexpr unsigned int kTopFiveBitsMask = 0xF8u;
		constexpr unsigned int kFourByteLeadPattern = 0xF0u;

		const auto byte = static_cast<unsigned char>(leadByte);

		if (byte < kFirstNonAsciiByte)
		{
			return 1;
		}

		if ((byte & kTopThreeBitsMask) == kTwoByteLeadPattern)
		{
			return 2;
		}

		if ((byte & kTopFourBitsMask) == kThreeByteLeadPattern)
		{
			return 3;
		}

		if ((byte & kTopFiveBitsMask) == kFourByteLeadPattern)
		{
			return 4;
		}

		return 1;
	}

	std::size_t NextCharacter(std::string_view bytes, std::size_t offset);

	std::size_t PreviousCharacter(std::string_view bytes, std::size_t offset);

	std::size_t CharacterBoundaryAtOrBefore(std::string_view bytes, std::size_t offset);

	std::size_t CharacterBoundaryAtOrAfter(std::string_view bytes, std::size_t offset);

	std::size_t CharacterCount(std::string_view bytes);

	// From 0 to the end, both included, so a prefix of any whole number of characters is bytes.substr(0, boundaries[n]).
	// 0 is a boundary even before a stray continuation byte, so text that is not valid UTF-8 still has a start.
	std::vector<std::size_t> CharacterBoundaries(std::string_view bytes);

	std::string_view EncodingName(Encoding encoding);
}