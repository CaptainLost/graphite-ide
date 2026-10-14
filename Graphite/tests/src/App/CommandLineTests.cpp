#include <doctest/doctest.h>

#include "App/CommandLine.h"

#include <filesystem>
#include <string>
#include <vector>

namespace {
	using Graphite::CommandLine;
	using Graphite::ParseCommandLine;

	using ArgumentList = std::vector<std::wstring>;

	// WARNING: Spelt out in escapes so the source file's encoding cannot change what is tested.
	const std::wstring kTortoise = L"\x017B\x00F3\x0142w";
}

TEST_CASE("CommandLine: no arguments name nothing")
{
	const CommandLine parsed = ParseCommandLine(ArgumentList{});

	CHECK_FALSE(parsed.UserData.has_value());
	CHECK(parsed.Folder.empty());
}

TEST_CASE("CommandLine: a lone argument is the folder")
{
	const CommandLine parsed = ParseCommandLine(ArgumentList{L"D:\\Work\\Proj"});

	CHECK_FALSE(parsed.UserData.has_value());
	CHECK(parsed.Folder == std::filesystem::path(L"D:\\Work\\Proj"));
}

TEST_CASE("CommandLine: the option comes first and the folder after it")
{
	const CommandLine parsed = ParseCommandLine(ArgumentList{L"--user-data", L"D:\\Data", L"D:\\Work\\Proj"});

	REQUIRE(parsed.UserData.has_value());
	CHECK(*parsed.UserData == std::filesystem::path(L"D:\\Data"));
	CHECK(parsed.Folder == std::filesystem::path(L"D:\\Work\\Proj"));
}

TEST_CASE("CommandLine: the folder comes first and the option after it")
{
	const CommandLine parsed = ParseCommandLine(ArgumentList{L"D:\\Work\\Proj", L"--user-data", L"D:\\Data"});

	REQUIRE(parsed.UserData.has_value());
	CHECK(*parsed.UserData == std::filesystem::path(L"D:\\Data"));
	CHECK(parsed.Folder == std::filesystem::path(L"D:\\Work\\Proj"));
}

TEST_CASE("CommandLine: the option without a value redirects nothing and takes no folder")
{
	const CommandLine parsed = ParseCommandLine(ArgumentList{L"--user-data"});

	CHECK_FALSE(parsed.UserData.has_value());
	CHECK(parsed.Folder.empty());
}

TEST_CASE("CommandLine: the option without a value does not take the folder before it")
{
	const CommandLine parsed = ParseCommandLine(ArgumentList{L"D:\\Work\\Proj", L"--user-data"});

	CHECK_FALSE(parsed.UserData.has_value());
	CHECK(parsed.Folder == std::filesystem::path(L"D:\\Work\\Proj"));
}

TEST_CASE("CommandLine: only the first folder counts")
{
	const CommandLine parsed = ParseCommandLine(ArgumentList{L"D:\\One", L"D:\\Two"});

	CHECK(parsed.Folder == std::filesystem::path(L"D:\\One"));
}

TEST_CASE("CommandLine: a folder outside the ANSI code page survives")
{
	const CommandLine parsed =
		ParseCommandLine(ArgumentList{L"--user-data", L"D:\\Dane\\" + kTortoise, L"D:\\Prace\\" + kTortoise});

	REQUIRE(parsed.UserData.has_value());
	CHECK(*parsed.UserData == std::filesystem::path(L"D:\\Dane\\" + kTortoise));
	CHECK(parsed.Folder == std::filesystem::path(L"D:\\Prace\\" + kTortoise));
}
