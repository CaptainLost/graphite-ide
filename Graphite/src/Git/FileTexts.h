#pragma once

#include "Core/Base/Result.h"

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

	// One side of a file: its text, and whether that side had the file at all. A NotFound
	// from the read means the file did not exist on that side - an added file has no old
	// side, a deleted one has no new side - which is an answer rather than a failure.
	// Anything else is a failure and stays one.
	struct SideText
	{
		std::string Text;
		bool Present = false;
	};

	// Turns a blob read into the answer ReadFileTexts needs for one side: present, absent,
	// or failed. Pulled out of RepositoryGitPort so this - the actual new reasoning this
	// task adds, as opposed to the port plumbing around it - can be tested without a
	// repository on disk.
	Core::Result<SideText> ReadSideText(Core::Result<std::string> read);
}
