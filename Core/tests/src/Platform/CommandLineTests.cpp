#include <doctest/doctest.h>

#include "Core/Base/PlatformDetection.h"

#ifdef CORE_PLATFORM_WINDOWS

#include "Core/Platform/CommandLine.h"

#include <Windows.h>
#include <shellapi.h>

#include <string>
#include <vector>

namespace {
	constexpr int kFirstArgumentAfterProgram = 1;

	std::vector<std::wstring> ArgumentsAsChildSeesThem(const std::wstring& line)
	{
		int partCount = 0;
		LPWSTR* parts = CommandLineToArgvW(line.c_str(), &partCount);
		REQUIRE(parts != nullptr);

		std::vector<std::wstring> arguments;
		for (int partIndex = kFirstArgumentAfterProgram; partIndex < partCount; ++partIndex)
		{
			arguments.emplace_back(parts[partIndex]);
		}

		LocalFree(parts);
		return arguments;
	}

	std::vector<std::wstring> ArgumentsAfterRoundTrip(const std::vector<std::string>& arguments)
	{
		return ArgumentsAsChildSeesThem(Core::BuildWindowsCommandLine("tool.exe", arguments));
	}
}

TEST_CASE("BuildWindowsCommandLine leaves a space-free program and arguments unquoted so the common line reads plainly in a debugger")
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

TEST_CASE("BuildWindowsCommandLine keeps an empty argument as an argument")
{
	CHECK(ArgumentsAfterRoundTrip({"a", "", "b"}) == std::vector<std::wstring>{L"a", L"", L"b"});
}

TEST_CASE("BuildWindowsCommandLine delivers an embedded quote intact")
{
	CHECK(ArgumentsAfterRoundTrip({"say \"hi\"", "a\"b"}) == std::vector<std::wstring>{L"say \"hi\"", L"a\"b"});
}

TEST_CASE("BuildWindowsCommandLine keeps backslashes that precede a quote")
{
	CHECK(ArgumentsAfterRoundTrip({"a\\\"b", "a\\\\\"b"}) == std::vector<std::wstring>{L"a\\\"b", L"a\\\\\"b"});
}

TEST_CASE("BuildWindowsCommandLine keeps a trailing backslash on a quoted argument")
{
	CHECK(ArgumentsAfterRoundTrip({"C:\\dir with space\\", "C:\\two\\\\ trailing\\\\"}) ==
		  std::vector<std::wstring>{L"C:\\dir with space\\", L"C:\\two\\\\ trailing\\\\"});
}

TEST_CASE("BuildWindowsCommandLine leaves backslashes alone when no quote follows them")
{
	CHECK(ArgumentsAfterRoundTrip({"C:\\plain\\path\\"}) == std::vector<std::wstring>{L"C:\\plain\\path\\"});
}

TEST_CASE("BuildWindowsCommandLine keeps an argument with a tab in one piece")
{
	CHECK(ArgumentsAfterRoundTrip({"a\tb"}) == std::vector<std::wstring>{L"a\tb"});
}

TEST_CASE("BuildWindowsCommandLine converts UTF-8 arguments rather than widening bytes")
{
	CHECK(ArgumentsAfterRoundTrip({"zażółć", "日本 語"}) == std::vector<std::wstring>{L"zażółć", L"日本 語"});
}

#endif
