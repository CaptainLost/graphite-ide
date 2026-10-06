#include "corepch.h"

#include "Core/Terminal/CharacterWidth.h"

namespace Core {

	namespace {
		struct CodePointRange
		{
			char32_t First;
			char32_t Last;
		};

		constexpr CodePointRange kHangulJamoInitialConsonants{.First = 0x1100, .Last = 0x115F};
		constexpr CodePointRange kCjkRadicalsKangxiAndSymbols{.First = 0x2E80, .Last = 0x303E};
		constexpr CodePointRange kHiraganaThroughCjkCompatibility{.First = 0x3041, .Last = 0x33FF};
		constexpr CodePointRange kCjkExtensionA{.First = 0x3400, .Last = 0x4DBF};
		constexpr CodePointRange kCjkUnifiedIdeographs{.First = 0x4E00, .Last = 0x9FFF};
		constexpr CodePointRange kYiSyllablesAndRadicals{.First = 0xA000, .Last = 0xA4CF};
		constexpr CodePointRange kHangulSyllables{.First = 0xAC00, .Last = 0xD7A3};
		constexpr CodePointRange kCjkCompatibilityIdeographs{.First = 0xF900, .Last = 0xFAFF};
		constexpr CodePointRange kFullwidthForms{.First = 0xFF00, .Last = 0xFF60};
		constexpr CodePointRange kEmoji{.First = 0x1F300, .Last = 0x1F64F};

		constexpr CodePointRange kWideRanges[] = {
			kHangulJamoInitialConsonants,
			kCjkRadicalsKangxiAndSymbols,
			kHiraganaThroughCjkCompatibility,
			kCjkExtensionA,
			kCjkUnifiedIdeographs,
			kYiSyllablesAndRadicals,
			kHangulSyllables,
			kCjkCompatibilityIdeographs,
			kFullwidthForms,
			kEmoji,
		};
	}

	std::uint8_t CharacterWidth(char32_t code)
	{
		for (const CodePointRange& range : kWideRanges)
		{
			if (code >= range.First && code <= range.Last)
			{
				return kWideCharacterCells;
			}
		}

		return kNarrowCharacterCells;
	}
}
