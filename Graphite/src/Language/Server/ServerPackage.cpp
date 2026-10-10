#include "Language/Server/ServerPackage.h"

namespace Graphite {

	const GitHubAsset* FindAssetFor(const GitHubReleasePackage& package, Core::PlatformTarget platform)
	{
		for (const GitHubAsset& asset : package.Assets)
		{
			if (asset.Platform == platform)
			{
				return &asset;
			}
		}

		return nullptr;
	}

	bool IsAvailableOn(const ServerPackage& package, Core::PlatformTarget platform)
	{
		const GitHubReleasePackage* release = std::get_if<GitHubReleasePackage>(&package);
		return release == nullptr || FindAssetFor(*release, platform) != nullptr;
	}
}
