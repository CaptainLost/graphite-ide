#pragma once

#include <filesystem>
#include <optional>
#include <string>

namespace Graphite {

	enum class RestrictedBarChoice
	{
		None,
		TrustFolder,
		TrustParent
	};

	RestrictedBarChoice DrawRestrictedModeBar(const std::optional<std::filesystem::path>& parent);

	std::string TrustAllMessage(const std::filesystem::path& parent);
}
