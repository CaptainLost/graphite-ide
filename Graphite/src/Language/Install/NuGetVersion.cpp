#include "Language/Install/NuGetVersion.h"

#include "Core/Base/Ascii.h"

#include <algorithm>
#include <charconv>
#include <system_error>

namespace Graphite {

	namespace {

		constexpr std::size_t kLargestNumberCount = 4;

		bool IsDigit(char character)
		{
			return character >= '0' && character <= '9';
		}

		bool IsAllDigits(std::string_view text)
		{
			for (const char character : text)
			{
				if (!IsDigit(character))
				{
					return false;
				}
			}

			return !text.empty();
		}

		bool IsLabelCharacter(char character)
		{
			const bool isLetter = (character >= 'a' && character <= 'z') || (character >= 'A' && character <= 'Z');
			return isLetter || IsDigit(character) || character == '-';
		}

		bool IsLabel(std::string_view text)
		{
			for (const char character : text)
			{
				if (!IsLabelCharacter(character))
				{
					return false;
				}
			}

			return !text.empty();
		}

		// Nothing for a number too large for 64 bits as well as for anything that is not a number.
		std::optional<std::uint64_t> NumberOf(std::string_view digits)
		{
			if (!IsAllDigits(digits))
			{
				return std::nullopt;
			}

			std::uint64_t value = 0;
			const std::from_chars_result parsed = std::from_chars(digits.data(), digits.data() + digits.size(), value);

			if (parsed.ec != std::errc{})
			{
				return std::nullopt;
			}

			return value;
		}

		// Keeps empty parts, so "1..0" fails rather than reading as "1.0".
		std::vector<std::string_view> SplitOnDots(std::string_view text)
		{
			std::vector<std::string_view> parts;
			std::size_t partStart = 0;

			for (;;)
			{
				const std::size_t dot = text.find('.', partStart);

				if (dot == std::string_view::npos)
				{
					parts.push_back(text.substr(partStart));
					return parts;
				}

				parts.push_back(text.substr(partStart, dot - partStart));
				partStart = dot + 1;
			}
		}

		// Two numbers compare by value, compared as digit strings so no length overflows.
		std::strong_ordering CompareLabels(std::string_view left, std::string_view right)
		{
			const bool leftIsNumber = IsAllDigits(left);
			const bool rightIsNumber = IsAllDigits(right);

			if (leftIsNumber && rightIsNumber)
			{
				const std::string_view leftDigits = Core::WithoutLeading(left, "0");
				const std::string_view rightDigits = Core::WithoutLeading(right, "0");

				if (leftDigits.size() != rightDigits.size())
				{
					return leftDigits.size() <=> rightDigits.size();
				}

				return leftDigits.compare(rightDigits) <=> 0;
			}

			if (leftIsNumber != rightIsNumber)
			{
				return leftIsNumber ? std::strong_ordering::less : std::strong_ordering::greater;
			}

			return left.compare(right) <=> 0;
		}
	}

	std::optional<NuGetVersion> NuGetVersion::Parse(std::string_view text)
	{
		const std::string_view withoutMetadata = text.substr(0, text.find('+'));
		const std::size_t labelStart = withoutMetadata.find('-');
		const std::vector<std::string_view> numberParts = SplitOnDots(withoutMetadata.substr(0, labelStart));

		if (numberParts.size() > kLargestNumberCount)
		{
			return std::nullopt;
		}

		NuGetVersion version;

		for (std::size_t partIndex = 0; partIndex < numberParts.size(); ++partIndex)
		{
			const std::optional<std::uint64_t> number = NumberOf(numberParts[partIndex]);

			if (!number)
			{
				return std::nullopt;
			}

			version.m_Numbers[partIndex] = *number;
		}

		if (labelStart == std::string_view::npos)
		{
			return version;
		}

		for (const std::string_view label : SplitOnDots(withoutMetadata.substr(labelStart + 1)))
		{
			if (!IsLabel(label))
			{
				return std::nullopt;
			}

			version.m_Labels.push_back(Core::Lowered(label));
		}

		return version;
	}

	bool NuGetVersion::IsPrerelease() const
	{
		return !m_Labels.empty();
	}

	std::strong_ordering NuGetVersion::operator<=>(const NuGetVersion& other) const
	{
		if (const std::strong_ordering byNumbers = m_Numbers <=> other.m_Numbers; byNumbers != 0)
		{
			return byNumbers;
		}

		if (!IsPrerelease() || !other.IsPrerelease())
		{
			return other.IsPrerelease() <=> IsPrerelease();
		}

		const std::size_t sharedLabelCount = std::min(m_Labels.size(), other.m_Labels.size());

		for (std::size_t labelIndex = 0; labelIndex < sharedLabelCount; ++labelIndex)
		{
			if (const std::strong_ordering byLabel = CompareLabels(m_Labels[labelIndex], other.m_Labels[labelIndex]);
				byLabel != 0)
			{
				return byLabel;
			}
		}

		return m_Labels.size() <=> other.m_Labels.size();
	}

	bool NuGetVersion::operator==(const NuGetVersion& other) const
	{
		return (*this <=> other) == 0;
	}
}
