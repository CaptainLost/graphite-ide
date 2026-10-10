#pragma once

#include "Language/Install/InstalledServer.h"
#include "Language/Install/ToolSettings.h"

#include <doctest/doctest.h>

#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <string_view>

inline constexpr std::string_view kCsharpLsAssembly = "tools/net10.0/any/CSharpLanguageServer.dll";

// Lays csharp-ls out as the installer leaves it and returns its version directory.
inline std::filesystem::path InstallCsharpLsIn(const std::filesystem::path& serversDirectory)
{
	const std::filesystem::path serverDirectory = Graphite::ServerDirectoryOf(serversDirectory, "csharp-ls");
	const std::filesystem::path assembly = serverDirectory / "0.28.0" / kCsharpLsAssembly;

	std::filesystem::create_directories(assembly.parent_path());
	std::ofstream{assembly} << "an assembly";

	const Graphite::InstallRecord record{.Version = "0.28.0",
										 .SourceUrl = "https://example.invalid/csharp-ls.0.28.0.nupkg",
										 .VerifiedChecksum = std::nullopt,
										 .InstalledAt = "2026-10-11T00:00:00Z",
										 .Command = {.Runner = Graphite::LaunchRunner::Dotnet, .RelativePath = std::string{kCsharpLsAssembly}}};
	REQUIRE(Graphite::WriteInstallRecord(serverDirectory, record));

	return serverDirectory / "0.28.0";
}
