#include "corepch.h"

#include "Core/Platform/CommandLine.h"

#ifdef CORE_PLATFORM_WINDOWS

namespace Core {

	namespace {
		std::wstring QuoteIfNeeded(const std::wstring& piece)
		{
			if (piece.find(L' ') == std::wstring::npos)
			{
				return piece;
			}

			return L'"' + piece + L'"';
		}
	}

	std::wstring BuildWindowsCommandLine(const std::filesystem::path& program,
										 const std::vector<std::string>& arguments)
	{
		std::wstring commandLine = QuoteIfNeeded(program.wstring());

		for (const std::string& argument : arguments)
		{
			const std::wstring wide{argument.begin(), argument.end()};

			commandLine.push_back(L' ');
			commandLine += QuoteIfNeeded(wide);
		}

		return commandLine;
	}
}

#endif
