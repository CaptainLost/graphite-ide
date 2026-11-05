#pragma once

#include "Core/Git/Head.h"

#include <string_view>

namespace Graphite {

	// How HEAD's history stands against the line read on screen. The read lists the branches as they were when it was
	// read, so after a checkout it can name another branch checked out than HEAD now does.
	enum class HeadInRead
	{
		Agrees,       // the read's checked-out branch is HEAD's: its line is HEAD's history's
		WaitsForRead, // they differ while a read is coming: HEAD's history waits for it
		Differs       // they differ and no read is coming: HEAD's history is shown alone, with no line
	};

	struct HeadInReadInput
	{
		const Core::HeadState* Head = nullptr; // null while unread and after a failed read
		std::string_view ReadCheckedOut;       // the full name of the read's checked-out branch; empty when it names none
		bool ReadComing = false;               // a line read, or the branch listing one is made from, is on its way
	};

	HeadInRead PlaceHeadInRead(const HeadInReadInput& input);
}
