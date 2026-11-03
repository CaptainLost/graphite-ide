#pragma once

#include <string>

namespace Graphite {

	// What every host of the Changes view shares, owned by GraphiteLayer: a message started in one host shows in
	// the other. Selection, open folders and dialogs stay with each view.
	struct ChangesShared
	{
		std::string Message;
		bool Amend = false;
	};
}
