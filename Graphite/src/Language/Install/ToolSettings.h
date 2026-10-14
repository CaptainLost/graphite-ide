#pragma once

#include "Core/Base/Result.h"
#include "Core/Platform/CurrentPlatform.h"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace Graphite {

	enum class LaunchRunner : std::uint8_t
	{
		Executable,
		Dotnet
	};

	std::string_view RunnerName(LaunchRunner runner);

	std::optional<LaunchRunner> RunnerNamed(std::string_view name);

	struct ToolCommand
	{
		LaunchRunner Runner = LaunchRunner::Executable;
		std::string EntryPoint;
	};

	Core::Result<ToolCommand> ParseDotnetToolSettings(std::string_view xml);

	struct InstalledCommand
	{
		LaunchRunner Runner = LaunchRunner::Executable;
		std::string RelativePath;

		bool operator==(const InstalledCommand&) const = default;
	};

	Core::Result<InstalledCommand> FindToolCommand(const std::filesystem::path& packageDirectory, Core::PlatformTarget platform);
}
