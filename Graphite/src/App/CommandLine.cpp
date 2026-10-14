#include "App/CommandLine.h"

#include "Core/Base/PlatformDetection.h"

#ifdef CORE_PLATFORM_WINDOWS
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <shellapi.h>
#pragma comment(lib, "shell32.lib")
#endif

namespace Graphite {

	namespace {
		constexpr const wchar_t* kUserDataOption = L"--user-data";
		constexpr int kFirstArgumentAfterProgramName = 1;
	}

	CommandLine ParseCommandLine(const std::vector<std::wstring>& arguments)
	{
		CommandLine result;
		bool haveFolder = false;

		for (std::size_t argumentIndex = 0; argumentIndex < arguments.size(); ++argumentIndex)
		{
			const std::wstring& argument = arguments[argumentIndex];

			if (argument == kUserDataOption)
			{
				if (argumentIndex + 1 < arguments.size())
				{
					++argumentIndex;
					result.UserData = std::filesystem::path(arguments[argumentIndex]);
				}
				continue;
			}

			if (!haveFolder)
			{
				result.Folder = std::filesystem::path(argument);
				haveFolder = true;
			}
		}

		return result;
	}

	std::vector<std::wstring> ReadProcessCommandLine()
	{
		std::vector<std::wstring> arguments;

#ifdef CORE_PLATFORM_WINDOWS
		int argumentCount = 0;
		wchar_t** argv = ::CommandLineToArgvW(::GetCommandLineW(), &argumentCount);
		if (argv == nullptr)
		{
			return arguments;
		}

		for (int argumentIndex = kFirstArgumentAfterProgramName; argumentIndex < argumentCount; ++argumentIndex)
		{
			arguments.emplace_back(argv[argumentIndex]);
		}

		::LocalFree(argv);
#endif

		return arguments;
	}
}
