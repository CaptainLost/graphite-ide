#include "corepch.h"

#include "Core/Base/Count.h"

namespace Core {

	namespace {
		constexpr std::size_t kDigitsPerGroup = 3;
	}

	std::string FormatCount(std::uint64_t count)
	{
		const std::string digits = std::to_string(count);

		std::string grouped;
		grouped.reserve(digits.size() + digits.size() / kDigitsPerGroup);

		for (std::size_t digitIndex = 0; digitIndex < digits.size(); ++digitIndex)
		{
			const std::size_t digitsFromRight = digits.size() - digitIndex;
			const bool startsGroup = digitIndex > 0 && digitsFromRight % kDigitsPerGroup == 0;

			if (startsGroup)
			{
				grouped += ',';
			}

			grouped += digits[digitIndex];
		}

		return grouped;
	}
}
