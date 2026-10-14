#include "Language/Server/DotnetRuntime.h"

#include "Language/Install/NuGetVersion.h"
#include "Language/Install/PackageNames.h"

#include "Core/FileSystem/FileSystem.h"
#include "Core/Platform/CurrentPlatform.h"
#include "Core/Platform/Process.h"
#include "Core/Platform/ShellProfiles.h"

#include <algorithm>
#include <charconv>
#include <format>
#include <optional>
#include <string_view>
#include <system_error>
#include <utility>

namespace Graphite {

	namespace {

		std::string DotnetExecutableName()
		{
			return "dotnet" + std::string{ExecutableSuffix(Core::CurrentPlatform().System)};
		}

		bool IsRegularFile(const std::filesystem::path& path)
		{
			std::error_code failure;
			return std::filesystem::is_regular_file(path, failure) && !failure;
		}

		std::optional<int> MajorOf(std::string_view version)
		{
			int major = 0;
			const std::from_chars_result parsed = std::from_chars(version.data(), version.data() + version.size(), major);

			if (parsed.ec != std::errc{} || parsed.ptr == version.data())
			{
				return std::nullopt;
			}

			return major;
		}

		bool IsRuntimeVersion(std::string_view name)
		{
			return MajorOf(name).has_value() && NuGetVersion::Parse(name).has_value();
		}

		bool IsHigherVersion(const std::string& left, const std::string& right)
		{
			return *NuGetVersion::Parse(left) > *NuGetVersion::Parse(right);
		}

		std::vector<std::string> RuntimeVersionsIn(const std::filesystem::path& root)
		{
			std::vector<std::string> versions;
			std::error_code listError;
			std::filesystem::directory_iterator entries{root / "shared" / "Microsoft.NETCore.App", listError};

			for (; !listError && entries != std::filesystem::directory_iterator{}; entries.increment(listError))
			{
				std::error_code kindError;
				std::string name = Core::ToUtf8(entries->path().filename());

				if (entries->is_directory(kindError) && IsRuntimeVersion(name))
				{
					versions.push_back(std::move(name));
				}
			}

			std::sort(versions.begin(), versions.end(), IsHigherVersion);
			return versions;
		}

		bool IsSameDirectory(const std::filesystem::path& left, const std::filesystem::path& right)
		{
			return Core::IsSameOrWithin(left, right) && Core::IsSameOrWithin(right, left);
		}

		bool AlreadyFound(const std::vector<DotnetInstallation>& found, const std::filesystem::path& root)
		{
			for (const DotnetInstallation& installation : found)
			{
				if (IsSameDirectory(installation.Executable.parent_path(), root))
				{
					return true;
				}
			}

			return false;
		}
	}

	std::vector<DotnetInstallation> FindDotnetInstallations(std::span<const std::filesystem::path> candidateRoots)
	{
		std::vector<DotnetInstallation> found;

		for (const std::filesystem::path& root : candidateRoots)
		{
			const std::filesystem::path executable = root / Core::FromUtf8(DotnetExecutableName());

			if (AlreadyFound(found, root) || !IsRegularFile(executable))
			{
				continue;
			}

			std::vector<std::string> runtimes = RuntimeVersionsIn(root);

			if (runtimes.empty())
			{
				continue;
			}

			found.push_back(DotnetInstallation{.Executable = executable, .RuntimeVersions = std::move(runtimes)});
		}

		return found;
	}

	std::vector<std::filesystem::path> DotnetCandidateRoots()
	{
		std::vector<std::filesystem::path> roots;

		if (std::optional<std::filesystem::path> configured = Core::EnvironmentPath(L"DOTNET_ROOT"))
		{
			roots.push_back(std::move(*configured));
		}

		if (const std::filesystem::path onPath = Core::FindOnPath(DotnetExecutableName()); !onPath.empty())
		{
			roots.push_back(onPath.parent_path());
		}

		if (std::optional<std::filesystem::path> programFiles = Core::EnvironmentPath(L"ProgramFiles"))
		{
			roots.push_back(*programFiles / "dotnet");
		}

		return roots;
	}

	bool HasRuntimeAtLeast(const DotnetInstallation& dotnet, int minimumMajor)
	{
		for (const std::string& version : dotnet.RuntimeVersions)
		{
			if (MajorOf(version).value_or(0) >= minimumMajor)
			{
				return true;
			}
		}

		return false;
	}

	const DotnetInstallation* FirstWithRuntimeAtLeast(std::span<const DotnetInstallation> installations, int minimumMajor)
	{
		for (const DotnetInstallation& installation : installations)
		{
			if (HasRuntimeAtLeast(installation, minimumMajor))
			{
				return &installation;
			}
		}

		return nullptr;
	}

	std::string DotnetDownloadUrl(int major)
	{
		return std::format("https://dotnet.microsoft.com/download/dotnet/{}.0", major);
	}
}
