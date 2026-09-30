#pragma once

#include <filesystem>
#include <optional>

namespace Graphite {

	enum class RestrictedBarChoice
	{
		None,
		TrustFolder,
		TrustParent
	};

	// The bar shown above the editor while the open folder is not trusted: what restricted
	// mode switches off, and the one click that ends it. The second button appears only when
	// there is a parent worth offering (see ParentWorthTrusting). Returns what was clicked this
	// frame; the caller does the trusting.
	RestrictedBarChoice DrawRestrictedModeBar(const std::optional<std::filesystem::path>& parent);
}
