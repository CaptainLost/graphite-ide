#include "corepch.h"

#include "Core/Base/Ascii.h"

#include <cctype>

namespace Core {

	std::string Lowered(std::string_view text)
	{
		std::string result;
		result.reserve(text.size());

		for (const char character : text)
		{
			result.push_back(LowerAscii(character));
		}

		return result;
	}

	std::string Trimmed(std::string_view text)
	{
		const auto notSpace = [](unsigned char character) { return std::isspace(character) == 0; };

		std::size_t begin = 0;
		while (begin < text.size() && !notSpace(static_cast<unsigned char>(text[begin])))
		{
			++begin;
		}

		std::size_t end = text.size();
		while (end > begin && !notSpace(static_cast<unsigned char>(text[end - 1])))
		{
			--end;
		}

		return std::string(text.substr(begin, end - begin));
	}
}