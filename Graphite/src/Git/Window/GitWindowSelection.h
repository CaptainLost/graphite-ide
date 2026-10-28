#pragma once

#include "Persistence/PersistedState.h"

#include <string>

namespace Graphite {

	struct GitWindowSelection
	{
		GitWindowView View = GitWindowView::Work;

		// Thread: its branch. History: its ref, empty for HEAD's.
		std::string Branch;

		friend bool operator==(const GitWindowSelection&, const GitWindowSelection&) = default;
	};
}
