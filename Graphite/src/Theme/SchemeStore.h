#pragma once

#include "Core/Base/Result.h"

#include "Theme/ColorScheme.h"

#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace Graphite {

	inline constexpr std::size_t kLongestSchemeName = 64;

	std::filesystem::path SchemeFile(const std::filesystem::path& directory, std::string_view name);

	std::vector<std::string> ListSchemes(const std::filesystem::path& directory);

	ColorScheme LoadScheme(const std::filesystem::path& file);

	Core::Result<void> SaveScheme(const std::filesystem::path& directory, std::string_view name,
								  const ColorScheme& scheme);

	Core::Result<void> DeleteScheme(const std::filesystem::path& directory, std::string_view name);

	std::optional<std::string> SchemeNameProblem(std::string_view name, const std::vector<std::string>& existing);

	Core::Result<void> RenameScheme(const std::filesystem::path& directory, std::string_view from,
									std::string_view to);
}
