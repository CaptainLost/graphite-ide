#pragma once

#include "Language/Install/ResolvedPackage.h"
#include "Language/Server/ServerPackage.h"

#include "Core/Base/Result.h"
#include "Core/Network/HttpClient.h"
#include "Core/Platform/CurrentPlatform.h"

namespace Graphite {

	// GitHub's "latest" is the newest release that is neither a draft nor a prerelease.
	Core::Result<ResolvedPackage> ResolveGitHubRelease(Core::HttpClient& http, const GitHubReleasePackage& package,
													   Core::PlatformTarget platform);
}
