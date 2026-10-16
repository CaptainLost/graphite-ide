#include "corepch.h"

#include "Core/Platform/CommandLine.h"

#include "Core/FileSystem/FileSystem.h"

namespace Core {

	namespace {
		constexpr char kQuote = '"';
		constexpr char kBackslash = '\\';

		constexpr std::string_view kCharactersThatSplitAnArgument = " \t\n\v\"";

		constexpr std::size_t kBackslashesPerLiteralBackslash = 2;
		constexpr std::size_t kBackslashEscapingAQuote = 1;

		bool NeedsQuoting(std::string_view argument)
		{
			const bool wouldVanish = argument.empty();
			const bool containsSplittingCharacter =
				argument.find_first_of(kCharactersThatSplitAnArgument) != std::string_view::npos;

			return wouldVanish || containsSplittingCharacter;
		}
	}

	// The rules CommandLineToArgvW and the C runtime split a line by: inside quotes, a run of
	// backslashes is literal unless a quote follows it, in which case each backslash must be
	// doubled and the quote itself escaped. The closing quote counts as a following quote,
	// which is what a trailing backslash would otherwise swallow.
	std::string QuoteWindowsArgument(std::string_view argument)
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

#ifdef CORE_PLATFORM_WINDOWS

namespace Core {

	namespace {
		constexpr wchar_t kWideSpace = L' ';
		constexpr wchar_t kWideQuote = L'"';

		// The program is split by other rules than the arguments: up to the next space, or up to
		// the closing quote, with no escaping at all. A Windows file name cannot contain a quote,
		// so wrapping it is all it ever needs.
		std::wstring QuoteProgram(const std::wstring& program)
		{
			const bool containsSpace = program.find(kWideSpace) != std::wstring::npos;

			if (!containsSpace)
			{
				return program;
			}

			return kWideQuote + program + kWideQuote;
		}
	}

	std::wstring BuildWindowsCommandLine(const std::filesystem::path& program,
										 const std::vector<std::string>& arguments)
	{
		std::wstring commandLine = QuoteProgram(program.wstring());

		for (const std::string& argument : arguments)
		{
			commandLine.push_back(kWideSpace);
			commandLine += ToUtf16(QuoteWindowsArgument(argument));
		}

		return commandLine;
	}
}

#endif