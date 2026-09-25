#pragma once

#include <string>

namespace Graphite {

	// Whether imgui.ini remembers a place for this window that it can still go back to:
	// floating on its own (no dock id), or in a dock node that still exists. A window reopened
	// from the last launch that has one goes back there by itself; docking it anywhere on its
	// first frame would move it somewhere it never was, and one that was floating is left to
	// float.
	//
	// `windowName` is the whole name passed to Begin, "###" part included: the settings are
	// keyed by the id ImGui hashes from it.
	bool HasRememberedPlace(const std::string& windowName);
}
