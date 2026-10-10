#include <doctest/doctest.h>

#include "Language/Install/PackageNames.h"

#include "Core/Platform/CurrentPlatform.h"

#include <ostream>
#include <string>
#include <string_view>

namespace {

	constexpr Core::PlatformTarget kWindowsX64{.System = Core::OperatingSystem::Windows, .Machine = Core::Architecture::X64};
	constexpr Core::PlatformTarget kLinuxArm64{.System = Core::OperatingSystem::Linux, .Machine = Core::Architecture::Arm64};
	constexpr Core::PlatformTarget kMacArm64{.System = Core::OperatingSystem::MacOS, .Machine = Core::Architecture::Arm64};

	std::string Expanded(std::string_view pattern, std::string_view version, Core::PlatformTarget platform)
	{
		const Core::Result<std::string> expanded =
			Graphite::ExpandPackageTemplate(pattern, {.Version = version, .Platform = platform});
		REQUIRE(expanded);
		return expanded.GetValue();
	}
}

TEST_CASE("A template's placeholders take the version and the platform's names")
{
	CHECK(Expanded("clangd-{os}-{version}.zip", "23.1.0", kWindowsX64) == "clangd-windows-23.1.0.zip");
	CHECK(Expanded("clangd_{version}/bin/clangd{exe}", "23.1.0", kWindowsX64) == "clangd_23.1.0/bin/clangd.exe");
	CHECK(Expanded("roslyn-language-server.{rid}", "", kWindowsX64) == "roslyn-language-server.win-x64");
	CHECK(Expanded("omnisharp-{rid}.zip", "", kLinuxArm64) == "omnisharp-linux-arm64.zip");
	CHECK(Expanded("tool-{os}-{arch}{exe}", "", kMacArm64) == "tool-mac-arm64");
	CHECK(Expanded("{rid}", "", kMacArm64) == "osx-arm64");
	CHECK(Expanded("no placeholders", "1.0", kWindowsX64) == "no placeholders");
}

TEST_CASE("An unknown placeholder or an unclosed brace is an error rather than text")
{
	CHECK_FALSE(Graphite::ExpandPackageTemplate("clangd-{platform}.zip", {.Version = "1", .Platform = kWindowsX64}));
	CHECK_FALSE(Graphite::ExpandPackageTemplate("clangd-{version.zip", {.Version = "1", .Platform = kWindowsX64}));
}

TEST_CASE("A version or an id names a directory only when it can neither leave nor be swept")
{
	CHECK(Graphite::IsSafeDirectoryName("23.1.0"));
	CHECK(Graphite::IsSafeDirectoryName("5.12.0-1.26475.2"));
	CHECK(Graphite::IsSafeDirectoryName("1.0.0+build.5"));
	CHECK(Graphite::IsSafeDirectoryName("csharp-ls"));

	CHECK_FALSE(Graphite::IsSafeDirectoryName(""));
	CHECK_FALSE(Graphite::IsSafeDirectoryName(".."));
	CHECK_FALSE(Graphite::IsSafeDirectoryName("../evil"));
	CHECK_FALSE(Graphite::IsSafeDirectoryName("a/b"));
	CHECK_FALSE(Graphite::IsSafeDirectoryName("-rc"));
	CHECK_FALSE(Graphite::IsSafeDirectoryName("1.0."));
	CHECK_FALSE(Graphite::IsSafeDirectoryName("1.0.TMP"));
	CHECK_FALSE(Graphite::IsSafeDirectoryName(".download"));
	CHECK_FALSE(Graphite::IsSafeDirectoryName(std::string(65, '1')));

	CHECK(Graphite::IsTemporaryName("24.0.0.tmp"));
	CHECK(Graphite::IsTemporaryName("installed.json.TMP"));
	CHECK_FALSE(Graphite::IsTemporaryName("tmp"));
}
