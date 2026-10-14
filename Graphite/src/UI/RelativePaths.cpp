#include "UI/RelativePaths.h"

#include "Core/Base/Ascii.h"
#include "Core/FileSystem/FileSystem.h"

#include <cstddef>

namespace Graphite {

	namespace {
		constexpr std::string_view kCharactersThatEndAPath = "'\"()[]<>,;:";
		constexpr std::string_view kCharactersThatOpenAToken = "'\"([<";
		constexpr std::size_t kSeparatorAndFirstNameCharacter = 2;
		constexpr std::size_t kNoPrefix = 0;
		constexpr std::size_t kDriveLetterAndColon = 2;

		bool EndsPath(char character)
		{
			return Core::IsAsciiWhitespace(character) || kCharactersThatEndAPath.find(character) != std::string_view::npos;
		}

		bool StartsToken(std::string_view message, std::size_t position)
		{
			if (position == 0)
			{
				return true;
			}

			const char before = message[position - 1];
			return Core::IsAsciiWhitespace(before) || kCharactersThatOpenAToken.find(before) != std::string_view::npos;
		}

		// WARNING: only the drive letter matches in either case: Windows hands it out either way,
		// while the rest of a path comes from the same place in the same case.
		bool SamePathCharacter(char messageCharacter, char rootCharacter, std::size_t characterIndex,
							   std::string_view root)
		{
			if (Core::IsPathSeparator(messageCharacter) && Core::IsPathSeparator(rootCharacter))
			{
				return true;
			}

			const bool isDriveLetter = characterIndex == 0 && root.size() >= kDriveLetterAndColon && root[1] == ':';

			if (isDriveLetter)
			{
				return Core::EqualsIgnoringCase(messageCharacter, rootCharacter);
			}

			return messageCharacter == rootCharacter;
		}

		std::size_t RootPrefixAt(std::string_view message, std::size_t position, std::string_view root)
		{
			if (message.size() - position < root.size() + kSeparatorAndFirstNameCharacter)
			{
				return kNoPrefix;
			}

			for (std::size_t characterIndex = 0; characterIndex < root.size(); ++characterIndex)
			{
				if (!SamePathCharacter(message[position + characterIndex], root[characterIndex], characterIndex, root))
				{
					return kNoPrefix;
				}
			}

			const std::size_t separatorPosition = position + root.size();

			if (!Core::IsPathSeparator(message[separatorPosition]) || EndsPath(message[separatorPosition + 1]))
			{
				return kNoPrefix;
			}

			return root.size() + 1;
		}
	}

	std::string RelativiseUnder(std::string_view message, const std::filesystem::path& root)
	{
		std::string rootText = Core::ToUtf8(root);

		while (!rootText.empty() && Core::IsPathSeparator(rootText.back()))
		{
			rootText.pop_back();
		}

		// WARNING: checked once the separators are gone: a root of "/" alone would otherwise match
		// every absolute path in the message, inside the repository or not.
		if (rootText.empty())
		{
			return std::string(message);
		}

		std::string result;
		result.reserve(message.size());

		std::size_t messageIndex = 0;

		while (messageIndex < message.size())
		{
			if (StartsToken(message, messageIndex))
			{
				if (const std::size_t prefixLength = RootPrefixAt(message, messageIndex, rootText);
					prefixLength > kNoPrefix)
				{
					messageIndex += prefixLength;
					continue;
				}
			}

			result += message[messageIndex];
			++messageIndex;
		}

		return result;
	}
}
