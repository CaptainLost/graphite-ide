#pragma once

#include <string>

namespace Graphite {

	// Both sides of one file's text around a commit. HasOld/HasNew exist because "absent
	// from that commit" and "an empty file" are different and both real: an added file has
	// no old side, a deleted file has no new side, and a genuinely empty file has a side
	// that is present and empty. A caller that cannot tell those apart would draw an added
	// file as an empty one.
	struct FileTexts
	{
		std::string Old;
		std::string New;
		bool HasOld = false;
		bool HasNew = false;
	};
}
