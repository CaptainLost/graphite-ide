#include "corepch.h"

#include "Core/Base/Ascii.h"

#include <cctype>

namespace Core {

	namespace {
		// std::isspace is undefined for a negative value other than EOF, and where char is signed every byte above 0x7F is negative.
		bool IsSpaceInCurrentLocale(char character)
		{
			return std::isspace(static_cast<unsigned char>(character)) != 0;
		}
	}

	std::string Lowered(std::string_view text)
	{
		std::string loweredText;
		loweredText.reserve(text.size());

		for (const char character : text)
		{
			loweredText.push_back(LowerAscii(character));
		}

		return loweredText;
	}

	std::string Trimmed(std::string_view text)
	{
		std::size_t firstKept = 0;
		while (firstKept < text.size() && IsSpaceInCurrentLocale(text[firstKept]))
		{
			++firstKept;
		}

		std::size_t afterLastKept = text.size();
		while (afterLastKept > firstKept && IsSpaceInCurrentLocale(text[afterLastKept - 1]))
		{
			--afterLastKept;
		}

		return std::string(text.substr(firstKept, afterLastKept - firstKept));
	}

	bool LoweredTextContains(std::string_view text, std::string_view loweredNeedle)
	{
		return Lowered(text).find(loweredNeedle) != std::string::npos;
	}
}