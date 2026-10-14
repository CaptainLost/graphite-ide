#pragma once

#include "Core/Platform/CurrentPlatform.h"

#include <cstdint>
#include <span>
#include <string_view>
#include <variant>

namespace Graphite {

	enum class ArchiveKind : std::uint8_t
	{
		Zip,
		TarGz
	};

	struct GitHubAsset
	{
		Core::PlatformTarget Platform;
		std::string_view AssetTemplate;
		ArchiveKind Archive = ArchiveKind::Zip;
		std::string_view ExecutableTemplate;
	};

	struct GitHubReleasePackage
	{
		std::string_view Repository;
		std::span<const GitHubAsset> Assets;
	};

	struct NuGetToolPackage
	{
		std::string_view PackageIdTemplate;
		bool AllowPrerelease = false;
	};

	using ServerPackage = std::variant<GitHubReleasePackage, NuGetToolPackage>;

	const GitHubAsset* FindAssetFor(const GitHubReleasePackage& package, Core::PlatformTarget platform);

	// WARNING: A NuGet tool names its build by runtime identifier, so it counts as available everywhere until NuGet says otherwise.
	bool IsAvailableOn(const ServerPackage& package, Core::PlatformTarget platform);
}
