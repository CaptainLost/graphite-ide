#include "Settings/ArgumentLine.h"

#include <cstddef>

namespace Graphite {

	namespace {

		constexpr char kQuote = '"';
		constexpr char kBackslash = '\\';
		constexpr char kSpace = ' ';
		constexpr char kTab = '\t';

		constexpr std::string_view kCharactersThatSplitAnArgument = " \t\n\v\"";

		constexpr std::size_t kBackslashesPerLiteralBackslash = 2;
		constexpr std::size_t kBackslashEscapingAQuote = 1;

		bool SeparatesArguments(char character)
		{
			return character == kSpace || character == kTab;
		}

		bool NeedsQuoting(std::string_view argument)
		{
			return argument.empty() || argument.find_first_of(kCharactersThatSplitAnArgument) != std::string_view::npos;
		}

		std::string Quoted(std::string_view argument)
		{
			if (!NeedsQuoting(argument))
			{
				return std::string{argument};
			}

			std::string quoted{kQuote};
			std::size_t pendingBackslashes = 0;

			for (const char character : argument)
			{
				if (character == kBackslash)
				{
					++pendingBackslashes;
					continue;
				}

				if (character == kQuote)
				{
					quoted.append(pendingBackslashes * kBackslashesPerLiteralBackslash + kBackslashEscapingAQuote, kBackslash);
				}
				else
				{
					quoted.append(pendingBackslashes, kBackslash);
				}

				pendingBackslashes = 0;
				quoted.push_back(character);
			}

			quoted.append(pendingBackslashes * kBackslashesPerLiteralBackslash, kBackslash);
			quoted.push_back(kQuote);
			return quoted;
		}
	}

	std::vector<std::string> SplitArgumentLine(std::string_view line)
	{
		std::vector<std::string> arguments;
		std::string current;
		bool inArgument = false;
		bool insideQuotes = false;
		std::size_t pendingBackslashes = 0;

		for (const char character : line)
		{
			if (character == kBackslash)
			{
				++pendingBackslashes;
				inArgument = true;
				continue;
			}

			if (character == kQuote)
			{
				const bool quoteIsEscaped = pendingBackslashes % kBackslashesPerLiteralBackslash == kBackslashEscapingAQuote;

				current.append(pendingBackslashes / kBackslashesPerLiteralBackslash, kBackslash);
				pendingBackslashes = 0;
				inArgument = true;

				if (quoteIsEscaped)
				{
					current.push_back(kQuote);
				}
				else
				{
					insideQuotes = !insideQuotes;
				}

				continue;
			}

			current.append(pendingBackslashes, kBackslash);
			pendingBackslashes = 0;

			if (SeparatesArguments(character) && !insideQuotes)
			{
				if (inArgument)
				{
					arguments.push_back(current);
					current.clear();
					inArgument = false;
				}

				continue;
			}

			current.push_back(character);
			inArgument = true;
		}

		current.append(pendingBackslashes, kBackslash);

		if (inArgument)
		{
			arguments.push_back(current);
		}

		return arguments;
	}

	std::string JoinArgumentLine(const std::vector<std::string>& arguments)
	{
		std::string line;

		for (const std::string& argument : arguments)
		{
			if (!line.empty())
			{
				line.push_back(kSpace);
			}

			line += Quoted(argument);
		}

		return line;
	}
}
