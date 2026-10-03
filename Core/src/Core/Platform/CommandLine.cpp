#include "corepch.h"

#include "Core/Platform/CommandLine.h"

#include "Core/Base/Narrow.h"

#ifdef CORE_PLATFORM_WINDOWS

#include <Windows.h>

namespace Core {

	namespace {
		// MultiByteToWideChar rather than a byte-per-character copy, which turns every
		// non-ASCII argument into mojibake. An invalid sequence becomes U+FFFD instead of
		// failing the launch: the argument still arrives, visibly damaged, where the caller
		// can see it.
		std::wstring Widen(const std::string& text)
		{
			if (text.empty())
			{
				return {};
			}

			const int size = Narrow<int>(text.size());
			const int length = MultiByteToWideChar(CP_UTF8, 0, text.data(), size, nullptr, 0);
			std::wstring wide(static_cast<std::size_t>(length), L'\0');
			MultiByteToWideChar(CP_UTF8, 0, text.data(), size, wide.data(), length);
			return wide;
		}

		bool NeedsQuoting(const std::wstring& argument)
		{
			return argument.empty() || argument.find_first_of(L" \t\n\v\"") != std::wstring::npos;
		}

		// The rules CommandLineToArgvW and the C runtime split a line by: inside quotes, a
		// run of backslashes is literal unless a quote follows it, in which case each
		// backslash must be doubled and the quote itself escaped. The closing quote counts
		// as a following quote, which is what a trailing backslash would otherwise swallow.
		std::wstring QuoteArgument(const std::wstring& argument)
		{
			if (!NeedsQuoting(argument))
			{
				return argument;
			}

			std::wstring quoted{L'"'};
			std::size_t backslashes = 0;

			for (const wchar_t character : argument)
			{
				if (character == L'\\')
				{
					++backslashes;
					continue;
				}

				if (character == L'"')
				{
					quoted.append(backslashes * 2 + 1, L'\\');
				}
				else
				{
					quoted.append(backslashes, L'\\');
				}

				backslashes = 0;
				quoted.push_back(character);
			}

			quoted.append(backslashes * 2, L'\\');
			quoted.push_back(L'"');
			return quoted;
		}

		// The program is split by different rules than the arguments - everything up to the
		// next space, or up to the closing quote, with no escaping at all. A Windows file name
		// cannot contain a quote, so wrapping it is all it ever needs.
		std::wstring QuoteProgram(const std::wstring& program)
		{
			if (program.find(L' ') == std::wstring::npos)
			{
				return program;
			}

			return L'"' + program + L'"';
		}
	}

	std::wstring BuildWindowsCommandLine(const std::filesystem::path& program,
										 const std::vector<std::string>& arguments)
	{
		std::wstring commandLine = QuoteProgram(program.wstring());

		for (const std::string& argument : arguments)
		{
			commandLine.push_back(L' ');
			commandLine += QuoteArgument(Widen(argument));
		}

		return commandLine;
	}
}

#endif
