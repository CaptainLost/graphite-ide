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

	// The bar shown above the editor while the open folder is not trusted: what restricted
	// mode switches off, and the one click that ends it. The second button appears only when
	// there is a parent worth offering (see ParentWorthTrusting). Returns what was clicked this
	// frame; the caller does the trusting.
	RestrictedBarChoice DrawRestrictedModeBar(const std::optional<std::filesystem::path>& parent);

	// What the confirmation behind "Trust All in <parent>" tells the user: the full parent
	// path, then what trusting it means for every folder under it. One click on the bar must
	// not trust a folder like Downloads without this being read first.
	std::string TrustAllMessage(const std::filesystem::path& parent);
}
