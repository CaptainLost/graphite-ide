#pragma once

#include "Language/Api/LanguageId.h"

#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace Graphite {

	enum class AdapterKind : std::uint8_t
	{
		Plain,
		Clangd
	};

	struct ServerDefinition
	{
		std::string_view Id;
		std::string_view DisplayName;
		std::string_view ExecutableName;
		std::string_view ProtocolLanguageId;
		std::span<const std::string_view> DefaultArguments;
		std::span<const std::string_view> RootMarkers;
		AdapterKind Adapter = AdapterKind::Plain;
	};

	std::span<const ServerDefinition> AllServers();

	const ServerDefinition* FindServer(std::string_view id);

	const ServerDefinition* RecommendedServerFor(LanguageId language);

	std::vector<std::string> DefaultArgumentsOf(const ServerDefinition& server);

	// The nearest marker wins whatever its kind: a directory naming a build is a better root than one above it naming only a checkout.
	std::filesystem::path RootFor(const ServerDefinition& server, const std::filesystem::path& file,
								  const std::filesystem::path& fallbackRoot);

	// A submodule carries its own .git, so a walk stops here rather than reading somebody else's project.
	bool IsProjectRoot(const std::filesystem::path& directory);
}
