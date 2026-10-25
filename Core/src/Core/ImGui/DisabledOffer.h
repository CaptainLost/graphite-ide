#pragma once

namespace Core {

	// WARNING: Call right after the item. A disabled item answers no click, so one that still has something to offer
	// while it cannot run says so in a tooltip, and this answers whether it was clicked all the same. An item that is
	// not disabled gets neither.
	bool OfferWhileDisabled(const char* note);
}
