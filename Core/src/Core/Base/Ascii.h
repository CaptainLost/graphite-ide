#pragma once

#include <cstddef>
#include <string>
#include <string_view>

namespace Core {

	// ASCII only, deliberately: every caller compares bytes - extensions, header names, ids,
	// search text - and a locale's fold would promise a correctness across scripts that a byte
	// comparison cannot keep. Every other byte comes back as it was.
	constexpr char LowerAscii(char character)
	{
		return character >= 'A' && character <= 'Z' ? static_cast<char>(character - 'A' + 'a') : character;
	}

	// Whole-string equality through LowerAscii.
	constexpr bool EqualsIgnoringCase(std::string_view left, std::string_view right)
	{
		if (left.size() != right.size())
		{
			return false;
		}

		for (std::size_t index = 0; index < left.size(); ++index)
		{
			if (LowerAscii(left[index]) != LowerAscii(right[index]))
			{
				return false;
			}
		}

		return true;
	}

	std::string Lowered(std::string_view text);
	std::string Trimmed(std::string_view text);
}