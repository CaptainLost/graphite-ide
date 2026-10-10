#include "Language/Install/ToolSettings.h"

#include "Language/Install/PackageNames.h"

#include "Core/Base/Ascii.h"
#include "Core/FileSystem/FileSystem.h"
#include "Core/FileSystem/RelativePath.h"

#include <pugixml.hpp>

#include <array>
#include <charconv>
#include <compare>
#include <format>
#include <system_error>
#include <vector>

namespace Graphite {

	namespace {

		constexpr std::string_view kSettingsFileName = "DotnetToolSettings.xml";
		constexpr std::string_view kFrameworkPrefix = "net";
		constexpr std::uintmax_t kLargestSettingsFile = 1024 * 1024;

		struct FrameworkVersion
		{
			int Major = 0;
			int Minor = 0;

			std::strong_ordering operator<=>(const FrameworkVersion&) const = default;
		};

		struct SettingsCandidate
		{
			FrameworkVersion Framework;
			std::filesystem::path File;
		};

		// Compared as numbers, since "net10.0" sorts below "net8.0" as text.
		FrameworkVersion FrameworkVersionOf(std::string_view framework)
		{
			FrameworkVersion version;

			if (!framework.starts_with(kFrameworkPrefix))
			{
				return version;
			}

			const std::string_view numbers = framework.substr(kFrameworkPrefix.size());
			const char* const end = numbers.data() + numbers.size();
			const std::from_chars_result major = std::from_chars(numbers.data(), end, version.Major);

			if (major.ec != std::errc{} || major.ptr == end || *major.ptr != '.')
			{
				return version;
			}

			const std::from_chars_result minor = std::from_chars(major.ptr + 1, end, version.Minor);

			if (minor.ec != std::errc{})
			{
				version.Minor = 0;
			}

			return version;
		}

		std::vector<SettingsCandidate> SettingsCandidatesUnder(const std::filesystem::path& tools, Core::PlatformTarget platform)
		{
			std::vector<SettingsCandidate> candidates;
			const Core::Result<std::vector<Core::DirectoryEntry>> frameworks = Core::ReadDirectory(tools);

			if (!frameworks)
			{
				return candidates;
			}

			const std::string runtimeIdentifier = RuntimeIdentifier(platform);
			const std::array<std::string_view, 2> platformFolders{"any", runtimeIdentifier};

			for (const Core::DirectoryEntry& framework : frameworks.GetValue())
			{
				if (!framework.IsDirectory)
				{
					continue;
				}

				for (const std::string_view platformFolder : platformFolders)
				{
					const std::filesystem::path file =
						framework.Path / Core::FromUtf8(platformFolder) / Core::FromUtf8(kSettingsFileName);

					if (Core::PathExists(file))
					{
						candidates.push_back({.Framework = FrameworkVersionOf(framework.Name), .File = file});
					}
				}
			}

			return candidates;
		}

		const SettingsCandidate* NewestFramework(const std::vector<SettingsCandidate>& candidates)
		{
			const SettingsCandidate* newest = nullptr;

			for (const SettingsCandidate& candidate : candidates)
			{
				if (newest == nullptr || newest->Framework < candidate.Framework)
				{
					newest = &candidate;
				}
			}

			return newest;
		}
	}

	std::string_view RunnerName(LaunchRunner runner)
	{
		switch (runner)
		{
		case LaunchRunner::Executable:
			return "executable";
		case LaunchRunner::Dotnet:
			break;
		}

		return "dotnet";
	}

	std::optional<LaunchRunner> RunnerNamed(std::string_view name)
	{
		if (Core::EqualsIgnoringCase(name, "dotnet"))
		{
			return LaunchRunner::Dotnet;
		}

		if (Core::EqualsIgnoringCase(name, "executable"))
		{
			return LaunchRunner::Executable;
		}

		return std::nullopt;
	}

	Core::Result<ToolCommand> ParseDotnetToolSettings(std::string_view xml)
	{
		pugi::xml_document document;
		const pugi::xml_parse_result parsed = document.load_buffer(xml.data(), xml.size());

		if (!parsed)
		{
			return Core::Error{.Code = Core::ErrorCode::InvalidArgument,
							   .Message = std::format("DotnetToolSettings.xml cannot be read: {}", parsed.description())};
		}

		const pugi::xml_node command = document.child("DotNetCliTool").child("Commands").child("Command");

		if (!command)
		{
			return Core::Error{.Code = Core::ErrorCode::InvalidArgument, .Message = "DotnetToolSettings.xml names no command"};
		}

		const std::string_view runnerName = command.attribute("Runner").as_string();
		const std::string_view entryPoint = command.attribute("EntryPoint").as_string();
		const std::optional<LaunchRunner> runner = RunnerNamed(runnerName);

		if (!runner)
		{
			return Core::Error{.Code = Core::ErrorCode::Unsupported,
							   .Message = std::format("the tool runs through '{}', which Graphite cannot start", runnerName)};
		}

		if (!Core::IsSafeRelativePath(entryPoint))
		{
			return Core::Error{.Code = Core::ErrorCode::InvalidArgument,
							   .Message = std::format("the tool's entry point '{}' is not a path inside its package", entryPoint)};
		}

		return ToolCommand{.Runner = *runner, .EntryPoint = std::string{entryPoint}};
	}

	Core::Result<InstalledCommand> FindToolCommand(const std::filesystem::path& packageDirectory, Core::PlatformTarget platform)
	{
		const std::vector<SettingsCandidate> candidates = SettingsCandidatesUnder(packageDirectory / "tools", platform);
		const SettingsCandidate* newest = NewestFramework(candidates);

		if (newest == nullptr)
		{
			return Core::Error{.Code = Core::ErrorCode::NotFound,
							   .Message = std::format("the package has no DotnetToolSettings.xml under tools/<framework>/any "
													  "or tools/<framework>/{}",
													  RuntimeIdentifier(platform))};
		}

		const Core::Result<std::string> settingsText = Core::ReadFileBytes(newest->File, kLargestSettingsFile);

		if (!settingsText)
		{
			return settingsText.GetError();
		}

		const Core::Result<ToolCommand> command = ParseDotnetToolSettings(settingsText.GetValue());

		if (!command)
		{
			return command.GetError();
		}

		const std::filesystem::path entryPoint = newest->File.parent_path() / Core::FromUtf8(command.GetValue().EntryPoint);

		if (!Core::PathExists(entryPoint))
		{
			return Core::Error{.Code = Core::ErrorCode::NotFound,
							   .Message = std::format("the package names '{}' as its command, which is not in it",
													  command.GetValue().EntryPoint)};
		}

		return InstalledCommand{.Runner = command.GetValue().Runner,
								.RelativePath = Core::ToGenericUtf8(entryPoint.lexically_relative(packageDirectory))};
	}
}
