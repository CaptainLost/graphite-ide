#pragma once

#include "Core/Git/Ref.h"
#include "Core/Git/Tag.h"
#include "Persistence/PersistedState.h"

#include <optional>
#include <string>
#include <vector>

namespace Graphite {

	// The ref whose history a restored state shows once it is known whether it is listed: a tag among the tags, any other
	// ref among the branches. None, HEAD's history, when the state names none or one no longer listed.
	std::optional<std::string> RefToRestore(const GitWindowState& state, const std::vector<Core::Branch>& branches,
											const std::vector<Core::Tag>& tags);

	// Whether a listing is in, or will not come.
	struct RefListing
	{
		bool Listed = false;
		bool Failed = false;
	};

	// A state that names a ref waits for the listing that holds it, the tags' for a tag and the branches' for any other,
	// before it is applied, and until then the history names no reference: showing HEAD's meanwhile would draw a history
	// the window then leaves. A failed listing ends the wait on HEAD's history, as a ref that is gone does.
	bool RestoreWaitsForListing(const GitWindowState& state, RefListing branches, RefListing tags);
}
