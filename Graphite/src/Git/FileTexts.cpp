#include "Git/FileTexts.h"

#include <utility>

namespace Graphite {

	Core::Result<SideText> ReadSideText(Core::Result<std::string> read)
	{
		if (read.HasValue())
		{
			return SideText{std::move(read).GetValue(), true};
		}

		// NotFound is the ordinary answer for a side that did not exist in that commit,
		// not a failure - see the comment on SideText. Anything else is a real failure and
		// is passed through as one.
		if (read.GetError().Code == Core::ErrorCode::NotFound)
		{
			return SideText{};
		}

		return read.GetError();
	}
}
