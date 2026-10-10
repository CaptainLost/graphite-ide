#pragma once

#include "Language/Install/ResolvedPackage.h"
#include "Language/Server/ServerPackage.h"

#include "Core/Base/Result.h"
#include "Core/Network/HttpClient.h"
#include "Core/Platform/CurrentPlatform.h"

namespace Graphite {

	// Versions come from the registration index rather than the package folder's list, because only the index says
	// which versions are unlisted.
	Core::Result<ResolvedPackage> ResolveNuGetTool(Core::HttpClient& http, const NuGetToolPackage& package,
												   Core::PlatformTarget platform);
}
