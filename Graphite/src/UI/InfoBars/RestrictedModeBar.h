#pragma once

#include <imgui.h>

#include <filesystem>
#include <optional>
#include <string>

namespace Graphite {

	enum class RestrictedBarChoice
	{
		None,
		TrustFolder,
		TrustParent,
		Close
	};

	RestrictedBarChoice DrawRestrictedModeBar(const std::optional<std::filesystem::path>& parent, const ImVec4& warningColour);

	std::string TrustAllMessage(const std::filesystem::path& parent);
}
