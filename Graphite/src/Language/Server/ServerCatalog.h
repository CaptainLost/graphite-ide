#pragma once

#include "Language/Api/LanguageId.h"
#include "Language/Server/ServerPackage.h"

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
		Clangd,
		Roslyn
	};

	enum class RuntimeKind : std::uint8_t
	{
		None,
		Dotnet
	};

	struct ServerRuntime
	{
		RuntimeKind Kind = RuntimeKind::None;
		int MinimumMajor = 0;
	};

	struct ServerDefinition
	{
		std::string_view Id;
		std::string_view DisplayName;
		std::string_view Description;
		std::string_view ExecutableName;
		std::string_view ProtocolLanguageId;
		std::span<const std::string_view> DefaultArguments;
		std::span<const std::string_view> RootMarkers;
		AdapterKind Adapter = AdapterKind::Plain;
		ServerPackage Package;
		ServerRuntime Runtime;
	};

	std::span<const ServerDefinition> AllServers();

	const ServerDefinition* FindServer(std::string_view id);

	std::string ServerDisplayName(std::string_view id);

	const ServerDefinition* RecommendedServerFor(LanguageId language);

	std::vector<std::string> DefaultArgumentsOf(const ServerDefinition& server);

	std::filesystem::path RootFor(const ServerDefinition& server, const std::filesystem::path& file,
								  const std::filesystem::path& fallbackRoot);

	bool DirectoryHoldsAnyMarker(const std::filesystem::path& directory, std::span<const std::string_view> markers);

	bool IsProjectRoot(const std::filesystem::path& directory);
}
