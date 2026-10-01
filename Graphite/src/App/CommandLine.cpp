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
	}

	CommandLine ParseCommandLine(const std::vector<std::wstring>& arguments)
	{
		CommandLine result;
		bool haveFolder = false;

		for (std::size_t i = 0; i < arguments.size(); ++i)
		{
			const std::wstring& argument = arguments[i];

			if (argument == kUserDataOption)
			{
				// The value is the next argument whatever it looks like; with none left the
				// option names nothing, and must not go on to swallow the folder.
				if (i + 1 < arguments.size())
				{
					++i;
					result.UserData = std::filesystem::path(arguments[i]);
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
		int count = 0;
		wchar_t** argv = ::CommandLineToArgvW(::GetCommandLineW(), &count);
		if (argv == nullptr)
		{
			return arguments;
		}

		// argv[0] is the program name.
		for (int i = 1; i < count; ++i)
		{
			arguments.emplace_back(argv[i]);
		}

		::LocalFree(argv);
#endif

		return arguments;
	}
}
