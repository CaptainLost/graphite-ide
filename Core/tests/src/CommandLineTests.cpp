#include <doctest/doctest.h>

#include "Core/Base/PlatformDetection.h"

#ifdef CORE_PLATFORM_WINDOWS

#include "Core/Platform/CommandLine.h"

TEST_CASE("BuildWindowsCommandLine leaves a space-free program and arguments unquoted")
{
	const std::wstring line = Core::BuildWindowsCommandLine("cmd.exe", {"/c", "echo"});

	CHECK(line == L"cmd.exe /c echo");
}

TEST_CASE("BuildWindowsCommandLine quotes a program path containing a space")
{
	const std::wstring line =
		Core::BuildWindowsCommandLine("C:\\Program Files\\tool\\tool.exe", {"/c"});

	CHECK(line == L"\"C:\\Program Files\\tool\\tool.exe\" /c");
}

TEST_CASE("BuildWindowsCommandLine quotes an argument containing a space")
{
	const std::wstring line = Core::BuildWindowsCommandLine("cmd.exe", {"/c", "echo two words"});

	CHECK(line == L"cmd.exe /c \"echo two words\"");
}

#endif
