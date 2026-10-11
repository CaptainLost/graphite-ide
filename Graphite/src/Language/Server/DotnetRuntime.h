#pragma once

#include <filesystem>
#include <span>
#include <string>
#include <vector>

namespace Graphite {

	struct DotnetInstallation
	{
		std::filesystem::path Executable;

		// Highest first.
		std::vector<std::string> RuntimeVersions;
	};

	std::vector<DotnetInstallation> FindDotnetInstallations(std::span<const std::filesystem::path> candidateRoots);

	std::vector<std::filesystem::path> DotnetCandidateRoots();

	std::string DotnetDownloadUrl(int major);

	bool HasRuntimeAtLeast(const DotnetInstallation& dotnet, int minimumMajor);

	const DotnetInstallation* FirstWithRuntimeAtLeast(std::span<const DotnetInstallation> installations, int minimumMajor);
}
