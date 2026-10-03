#include "corepch.h"

#include "Core/Term/CharacterWidth.h"

#include <array>

namespace Core {

	namespace {
		struct Range
		{
			char32_t First;
			char32_t Last;
		};

		constexpr std::array<Range, 10> WideRanges{{
			{0x1100, 0x115F},	// Hangul Jamo initial consonants
			{0x2E80, 0x303E},	// CJK radicals, Kangxi, CJK symbols
			{0x3041, 0x33FF},	// Hiragana through CJK compatibility
			{0x3400, 0x4DBF},	// CJK extension A
			{0x4E00, 0x9FFF},	// CJK unified ideographs
			{0xA000, 0xA4CF},	// Yi
			{0xAC00, 0xD7A3},	// Hangul syllables
			{0xF900, 0xFAFF},	// CJK compatibility ideographs
			{0xFF00, 0xFF60},	// Fullwidth forms
			{0x1F300, 0x1F64F}, // Emoji
		}};
	}

	std::uint8_t CharacterWidth(char32_t code)
	{
		for (const Range& range : WideRanges)
		{
			if (code >= range.First && code <= range.Last)
			{
				return 2;
			}
		}

		return 1;
	}
}
