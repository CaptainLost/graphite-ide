#pragma once

#include <filesystem>
#include <map>
#include <string>
#include <vector>

namespace Graphite {

	// What every host of the Changes view shares, owned by GraphiteLayer: a message started in one host shows in
	// the other. Selection, open folders and dialogs stay with each view.
	struct ChangesShared
	{
		std::string Message;
		bool Amend = false;

		// WARNING: Written when a discard is confirmed and read when it lands, because a directory
		// is gone from disk by then. An entry is erased as it is used; a failed discard leaves one
		// behind, and the map is cleared when the repository goes away. Shared because the side
		// panel alone takes landed discards, whichever host confirmed one.
		std::map<std::filesystem::path, std::vector<std::filesystem::path>> DiscardedFiles;
	};
}
