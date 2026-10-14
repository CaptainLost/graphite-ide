#pragma once

#include "Core/Text/DocumentSet.h"

#include <cstddef>
#include <filesystem>
#include <functional>
#include <optional>

namespace Graphite {

	std::optional<std::filesystem::path> ActiveTextDocument(const Core::DocumentSet& documents);

	std::optional<std::size_t> ShownActiveDocument(
		const Core::DocumentSet& documents,
		const std::function<bool(const std::filesystem::path&)>& isShown);

	std::optional<std::size_t> ShownActiveTab(
		const Core::DocumentSet& documents,
		const std::function<bool(const std::filesystem::path&)>& isShown);
}
