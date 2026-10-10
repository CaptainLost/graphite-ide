#include <doctest/doctest.h>

#include "Language/Install/ToolSettings.h"

#include "TempDirectory.h"

#include <ostream>
#include <string>
#include <string_view>

namespace {

	constexpr Core::PlatformTarget kWindowsX64{.System = Core::OperatingSystem::Windows, .Machine = Core::Architecture::X64};
	constexpr Core::PlatformTarget kLinuxX64{.System = Core::OperatingSystem::Linux, .Machine = Core::Architecture::X64};

	constexpr std::string_view kCsharpLsSettings =
		"<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"
		"<DotNetCliTool Version=\"1\">\n"
		"  <Commands>\n"
		"    <Command Name=\"csharp-ls\" EntryPoint=\"CSharpLanguageServer.dll\" Runner=\"dotnet\" />\n"
		"  </Commands>\n"
		"</DotNetCliTool>\n";

	constexpr std::string_view kRoslynSettings =
		"\xEF\xBB\xBF<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"
		"<DotNetCliTool Version=\"2\">\n"
		"  <Commands>\n"
		"    <Command Name=\"roslyn-language-server\" EntryPoint=\"roslyn-language-server.exe\" Runner=\"executable\" />\n"
		"  </Commands>\n"
		"</DotNetCliTool>\n";

	std::string SettingsRunning(std::string_view runner, std::string_view entryPoint)
	{
		return std::string{"<DotNetCliTool Version=\"1\"><Commands><Command Name=\"tool\" EntryPoint=\""} +
			   std::string{entryPoint} + "\" Runner=\"" + std::string{runner} + "\" /></Commands></DotNetCliTool>";
	}
}

TEST_CASE("csharp-ls's settings run a dll through dotnet")
{
	const Core::Result<Graphite::ToolCommand> command = Graphite::ParseDotnetToolSettings(kCsharpLsSettings);

	REQUIRE(command);
	CHECK(command.GetValue().Runner == Graphite::LaunchRunner::Dotnet);
	CHECK(command.GetValue().EntryPoint == "CSharpLanguageServer.dll");
}

TEST_CASE("Roslyn's settings with a byte order mark run an executable")
{
	const Core::Result<Graphite::ToolCommand> command = Graphite::ParseDotnetToolSettings(kRoslynSettings);

	REQUIRE(command);
	CHECK(command.GetValue().Runner == Graphite::LaunchRunner::Executable);
	CHECK(command.GetValue().EntryPoint == "roslyn-language-server.exe");
}

TEST_CASE("Settings that cannot be followed are refused")
{
	CHECK_FALSE(Graphite::ParseDotnetToolSettings(SettingsRunning("node", "tool.js")));
	CHECK_FALSE(Graphite::ParseDotnetToolSettings(SettingsRunning("dotnet", "../escape.dll")));
	CHECK_FALSE(Graphite::ParseDotnetToolSettings(SettingsRunning("dotnet", "C:/Windows/evil.dll")));
	CHECK_FALSE(Graphite::ParseDotnetToolSettings("<DotNetCliTool Version=\"1\"><Commands /></DotNetCliTool>"));
	CHECK_FALSE(Graphite::ParseDotnetToolSettings("this is not XML <"));
}

TEST_CASE("A runner's name reads back whatever its case")
{
	CHECK(Graphite::RunnerNamed("dotnet") == Graphite::LaunchRunner::Dotnet);
	CHECK(Graphite::RunnerNamed("Executable") == Graphite::LaunchRunner::Executable);
	CHECK_FALSE(Graphite::RunnerNamed("node").has_value());
	CHECK(Graphite::RunnerNamed(Graphite::RunnerName(Graphite::LaunchRunner::Dotnet)) == Graphite::LaunchRunner::Dotnet);
}

TEST_CASE("The command is found under any or under the platform's runtime identifier")
{
	TempDirectory temporaryDirectory;

	SUBCASE("any")
	{
		temporaryDirectory.MakeFile("tools/net10.0/any/DotnetToolSettings.xml", std::string{kCsharpLsSettings});
		temporaryDirectory.MakeFile("tools/net10.0/any/CSharpLanguageServer.dll", "an assembly");

		const Core::Result<Graphite::InstalledCommand> command =
			Graphite::FindToolCommand(temporaryDirectory.GetPath(), kWindowsX64);

		REQUIRE(command);
		CHECK(command.GetValue() == Graphite::InstalledCommand{.Runner = Graphite::LaunchRunner::Dotnet,
															   .RelativePath = "tools/net10.0/any/CSharpLanguageServer.dll"});
	}

	SUBCASE("the runtime identifier")
	{
		temporaryDirectory.MakeFile("tools/net10.0/win-x64/DotnetToolSettings.xml", std::string{kRoslynSettings});
		temporaryDirectory.MakeFile("tools/net10.0/win-x64/roslyn-language-server.exe", "an executable");

		const Core::Result<Graphite::InstalledCommand> onWindows =
			Graphite::FindToolCommand(temporaryDirectory.GetPath(), kWindowsX64);
		const Core::Result<Graphite::InstalledCommand> onLinux = Graphite::FindToolCommand(temporaryDirectory.GetPath(), kLinuxX64);

		REQUIRE(onWindows);
		CHECK(onWindows.GetValue().RelativePath == "tools/net10.0/win-x64/roslyn-language-server.exe");
		CHECK(onWindows.GetValue().Runner == Graphite::LaunchRunner::Executable);
		REQUIRE_FALSE(onLinux);
		CHECK(onLinux.GetError().Code == Core::ErrorCode::NotFound);
	}
}

TEST_CASE("With several frameworks the newest one's command is used")
{
	TempDirectory temporaryDirectory;
	temporaryDirectory.MakeFile("tools/net8.0/any/DotnetToolSettings.xml", SettingsRunning("dotnet", "Old.dll"));
	temporaryDirectory.MakeFile("tools/net8.0/any/Old.dll", "an old assembly");
	temporaryDirectory.MakeFile("tools/net10.0/any/DotnetToolSettings.xml", SettingsRunning("dotnet", "New.dll"));
	temporaryDirectory.MakeFile("tools/net10.0/any/New.dll", "a new assembly");

	const Core::Result<Graphite::InstalledCommand> command = Graphite::FindToolCommand(temporaryDirectory.GetPath(), kWindowsX64);

	REQUIRE(command);
	CHECK(command.GetValue().RelativePath == "tools/net10.0/any/New.dll");
}

TEST_CASE("A settings file whose command is missing from the package is refused")
{
	TempDirectory temporaryDirectory;
	temporaryDirectory.MakeFile("tools/net10.0/any/DotnetToolSettings.xml", std::string{kCsharpLsSettings});

	const Core::Result<Graphite::InstalledCommand> command = Graphite::FindToolCommand(temporaryDirectory.GetPath(), kWindowsX64);

	REQUIRE_FALSE(command);
	CHECK(command.GetError().Message.find("CSharpLanguageServer.dll") != std::string::npos);
}
